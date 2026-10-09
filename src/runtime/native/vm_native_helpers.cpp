#include <iostream>

#include "common/vm_native_helpers.h"
#include "common/vm_native_constants.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "vpp/runtime/value.h"
#include "vpp/core/message_constants.h"

namespace vietvm::helpers {

// Kiểm tra điều kiện của `hasEnvVar`.
bool hasEnvVar(const char *name) {
#if defined(_MSC_VER)
    char *value = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&value, &len, name);
    (void)len;
    if (err != 0 || value == nullptr) {
        return false;
    }
    std::free(value);
    return true;
#else
    return std::getenv(name) != nullptr;
#endif
}

// Lấy env var; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
std::optional<std::string> getEnvVar(const char *name) {
#if defined(_MSC_VER)
    char *value = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&value, &len, name);
    (void)len;
    if (err != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char *value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string(value);
#endif
}

// Chuyển một `StackValue` đối số thành chuỗi thô mà native helper cần; hàm giữ nội dung chuỗi nguyên bản và định dạng scalar theo quy tắc runtime.
std::string argToRawString(const StackValue &v) {
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return sv_to_string(v);
}

// Đọc số nguyên đã chuẩn hóa ở tầng V++. Primitive native chỉ chấp nhận đúng
// kiểu int của VM và không còn parse chuỗi hay ép số thực tích phân.
bool requireIntArgFromStack(const StackValue &arg,
                            const std::string &fn,
                            const std::string &label,
                            int &out,
                            std::string &err) {
    if (std::holds_alternative<int>(arg)) {
        out = std::get<int>(arg);
        return true;
    }
    err = vietvm::messages::formatMessage(
        vietvm::messages::kNativeInvalidArgument, {fn, label});
    return false;
}

// Tạo exception/thông báo lỗi khi số đối số native không đúng; hàm đóng gói tên hàm và arity mong đợi vào diagnostic thống nhất.
std::string nativeArgumentCountError(const std::string &fn, int expectedCount) {
    return vietvm::messages::formatMessage(
        vietvm::messages::kNativeArgumentCount, {fn, std::to_string(expectedCount)});
}

// Xác minh số đối số trên stack đúng arity yêu cầu; nếu sai hàm ném lỗi chuẩn trước khi native handler đọc tham số.
bool requireNativeArgumentCount(const std::vector<StackValue> &args,
                                const std::string &fn,
                                int expectedCount,
                                std::string &err) {
    if (args.size() == static_cast<std::size_t>(expectedCount)) {
        return true;
    }
    err = nativeArgumentCountError(fn, expectedCount);
    return false;
}

namespace {

// Lấy native handle đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
template <typename Handle>
bool getNativeHandleArgument(const std::vector<StackValue> &args,
                             std::size_t index,
                             const std::string &fn,
                             const char *typeName,
                             bool firstArgumentDiagnostic,
                             Handle &out,
                             std::string &err) {
    if (index >= args.size() || !std::holds_alternative<Handle>(args[index])) {
        err = messages::formatMessage(
            firstArgumentDiagnostic
                ? messages::kNativeHandleTypeRequiredFirstArgument
                : messages::kNativeHandleTypeRequired,
            {fn, typeName});
        return false;
    }
    out = std::get<Handle>(args[index]);
    if (out == nullptr) {
        err = messages::formatMessage(messages::kNativeHandleEmptyInternal, {fn, typeName});
        return false;
    }
    return true;
}

} // namespace

// Lấy first danh sách đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getFirstListArgument(const std::vector<StackValue> &args,
                          const std::string &fn,
                          ListHandle &out,
                          std::string &err) {
    return getNativeHandleArgument(args, 0, fn, "danh sách", true, out, err);
}

// Lấy danh sách đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getListArgument(const std::vector<StackValue> &args,
                     std::size_t index,
                     const std::string &fn,
                     ListHandle &out,
                     std::string &err) {
    return getNativeHandleArgument(args, index, fn, "danh sách", false, out, err);
}

// Lấy first ánh xạ đối số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getFirstMapArgument(const std::vector<StackValue> &args,
                         const std::string &fn,
                         MapHandle &out,
                         std::string &err) {
    return getNativeHandleArgument(args, 0, fn, "ánh xạ", true, out, err);
}

// Lấy non negative danh sách chỉ số; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
bool getNonNegativeListIndex(const StackValue &value, int &index, std::string &err) {
    if (!std::holds_alternative<int>(value)) {
        err = messages::messageText(messages::kNativeListIndexMustBeInteger);
        return false;
    }
    index = std::get<int>(value);
    if (index < 0) {
        err = messages::messageText(messages::kNativeListIndexOutOfRange);
        return false;
    }
    return true;
}

} // namespace vietvm::helpers
