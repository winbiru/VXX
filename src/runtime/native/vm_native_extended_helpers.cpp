#include "common/vm_native_extended_helpers.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/vm_native_helpers.h"
#include "vpp/core/text.h"

namespace vietvm::helpers {
namespace {

bool requireUtf8String(const StackValue &value,
                       const std::string &fn,
                       const char *label,
                       std::string &out,
                       std::string &err) {
    if (!std::holds_alternative<std::string>(value)) {
        err = fn + ": " + label + " phải là chuỗi";
        return false;
    }
    out = std::get<std::string>(value);
    if (!vietvm::core::isValidUtf8(out)) {
        err = fn + ": " + label + " không phải UTF-8 hợp lệ";
        return false;
    }
    return true;
}

bool compileRegex(const std::string &pattern,
                  std::regex &compiled,
                  std::string &err) {
    try {
        compiled = std::regex(pattern, std::regex::ECMAScript);
        return true;
    } catch (const std::regex_error &e) {
        err = std::string("regex: mẫu không hợp lệ: ") + e.what();
        return false;
    }
}

bool handleRegex(const std::string &fn,
                 const std::vector<StackValue> &args,
                 StackValue &result,
                 std::string &err) {
    if (fn != "regex_quet_noi_bo") return false;
    if (!requireNativeArgumentCount(args, fn, 2, err)) return true;

    std::string text;
    std::string pattern;
    if (!requireUtf8String(args[0], fn, "văn bản", text, err) ||
        !requireUtf8String(args[1], fn, "mẫu", pattern, err)) {
        return true;
    }

    std::regex compiled;
    if (!compileRegex(pattern, compiled, err)) return true;

    try {
        // Expose engine matches without match/search/replacement/split policy.
        // Prefixes are gaps since the previous match, not copies of the whole
        // prefix. Preserve raw byte fragments: std::regex matches UTF-8 by byte.
        std::vector<StackValue> matches;
        auto tail = text.cbegin();
        const std::sregex_iterator end;
        for (std::sregex_iterator it(text.begin(), text.end(), compiled); it != end; ++it) {
            std::vector<StackValue> groups;
            groups.reserve(it->size());
            for (const auto &group : *it) groups.push_back(make_string_value(group.str()));
            matches.push_back(make_list_value({
                make_string_value(it->prefix().str()),
                make_list_value(std::move(groups)),
            }));
            tail = (*it)[0].second;
        }
        result = make_list_value({
            make_list_value(std::move(matches)),
            make_string_value(std::string(tail, text.cend())),
        });
        return true;
    } catch (const std::regex_error &e) {
        err = std::string("regex: lỗi thực thi mẫu: ") + e.what();
        return true;
    }
}

enum class TaskStatus { Running, Done, Cancelled };

struct DelayedTask {
    std::mutex mutex;
    std::condition_variable cv;
    TaskStatus status = TaskStatus::Running;
    bool cancelRequested = false;
    StackValue result = make_null_value();
};

std::mutex &taskRegistryMutex() {
    static auto *value = new std::mutex();
    return *value;
}

std::unordered_map<int, std::shared_ptr<DelayedTask>> &taskRegistry() {
    static auto *value = new std::unordered_map<int, std::shared_ptr<DelayedTask>>();
    return *value;
}

std::atomic<int> &nextTaskId() {
    static auto *value = new std::atomic<int>(1);
    return *value;
}

std::shared_ptr<DelayedTask> findTask(int id) {
    std::lock_guard<std::mutex> lock(taskRegistryMutex());
    const auto it = taskRegistry().find(id);
    return it == taskRegistry().end() ? nullptr : it->second;
}

bool scalarTaskValue(const StackValue &value) {
    return std::holds_alternative<int>(value) ||
           std::holds_alternative<double>(value) ||
           std::holds_alternative<std::string>(value) ||
           std::holds_alternative<std::monostate>(value);
}

bool taskIdArg(const StackValue &value,
               const std::string &fn,
               int &id,
               std::string &err) {
    if (!requireIntArgFromStack(value, fn, "mã tác vụ", id, err)) return false;
    if (id <= 0) {
        err = fn + ": mã tác vụ phải dương";
        return false;
    }
    return true;
}

bool handleConcurrent(const std::string &fn,
                      const std::vector<StackValue> &args,
                      StackValue &result,
                      std::string &err) {
    if (fn != "task_native_exec") return false;
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;

    std::string operation;
    if (!requireUtf8String(args[0], fn, "thao tác", operation, err)) return true;

    if (operation == "schedule") {
        const std::string logicalFn = "tac_vu_sau";
        int delayMs = 0;
        if (!requireIntArgFromStack(args[1], logicalFn, "độ trễ mili giây", delayMs, err) ||
            delayMs < 0 || delayMs > 86400000) {
            if (err.empty()) err = "tác vụ sau: độ trễ phải trong 0..86400000 ms";
            return true;
        }
        if (!scalarTaskValue(args[2])) {
            err = "tác vụ sau: kết quả phiên bản 0.2 chỉ hỗ trợ scalar";
            return true;
        }
        const int id = nextTaskId().fetch_add(1);
        auto task = std::make_shared<DelayedTask>();
        task->result = args[2];
        {
            std::lock_guard<std::mutex> lock(taskRegistryMutex());
            taskRegistry()[id] = task;
        }
        std::thread([task, delayMs]() {
            std::unique_lock<std::mutex> lock(task->mutex);
            const bool cancelled = task->cv.wait_for(
                lock, std::chrono::milliseconds(delayMs),
                [&]() { return task->cancelRequested; });
            task->status = cancelled ? TaskStatus::Cancelled : TaskStatus::Done;
            lock.unlock();
            task->cv.notify_all();
        }).detach();
        result = make_int_value(id);
        return true;
    }

    if (operation == "wait") {
        const std::string logicalFn = "cho_tac_vu";
        int id = 0;
        int timeoutMs = 0;
        if (!taskIdArg(args[1], logicalFn, id, err) ||
            !requireIntArgFromStack(args[2], logicalFn, "thời gian chờ mili giây", timeoutMs, err) ||
            timeoutMs < 0) {
            if (err.empty()) err = "chờ tác vụ: thời gian chờ phải không âm";
            return true;
        }
        auto task = findTask(id);
        if (task == nullptr) {
            err = "chờ tác vụ: không tìm thấy tác vụ";
            return true;
        }
        std::unique_lock<std::mutex> lock(task->mutex);
        if (task->status == TaskStatus::Running) {
            task->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
                return task->status != TaskStatus::Running;
            });
        }
        result = task->status == TaskStatus::Done ? task->result : make_null_value();
        return true;
    }

    if (operation == "cancel") {
        const std::string logicalFn = "huy_tac_vu";
        int id = 0;
        if (!taskIdArg(args[1], logicalFn, id, err)) return true;
        auto task = findTask(id);
        if (task == nullptr) {
            err = "hủy tác vụ: không tìm thấy tác vụ";
            return true;
        }
        std::lock_guard<std::mutex> lock(task->mutex);
        if (task->status != TaskStatus::Running) {
            result = make_int_value(0);
            return true;
        }
        task->cancelRequested = true;
        task->status = TaskStatus::Cancelled;
        task->cv.notify_all();
        result = make_int_value(1);
        return true;
    }

    if (operation == "status") {
        const std::string logicalFn = "trang_thai_tac_vu";
        int id = 0;
        if (!taskIdArg(args[1], logicalFn, id, err)) return true;
        auto task = findTask(id);
        if (task == nullptr) {
            err = "trạng thái tác vụ: không tìm thấy tác vụ";
            return true;
        }
        std::lock_guard<std::mutex> lock(task->mutex);
        if (task->status == TaskStatus::Running) result = make_int_value(0);
        else if (task->status == TaskStatus::Done) result = make_int_value(1);
        else result = make_int_value(2);
        return true;
    }

    err = "task_native_exec: thao tác không được hỗ trợ";
    return true;
}

} // namespace

bool handleNativeExtendedLibraryFunction(const std::string &fn,
                                         const std::vector<StackValue> &args,
                                         StackValue &result,
                                         std::string &err) {
    if (handleRegex(fn, args, result, err)) return true;
    if (handleConcurrent(fn, args, result, err)) return true;
    return false;
}

} // namespace vietvm::helpers
