#include "common/vm_native_stdlib_helpers.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"

namespace vietvm::helpers {

namespace {

namespace fs = std::filesystem;

// Chuyển chuỗi UTF-8 thành `std::filesystem::path` theo quy tắc của nền tảng để đường dẫn Unicode hoạt động nhất quán.
fs::path utf8Path(const StackValue &value) {
    return fs::u8path(argToRawString(value));
}

// Chuyển `std::filesystem::path` thành chuỗi UTF-8 để runtime/API trả đường dẫn nhất quán giữa Windows và Unix.
std::string pathToUtf8(const fs::path &path) {
    return path.generic_u8string();
}

// Chuyển `std::error_code` filesystem thành lỗi runtime có ngữ cảnh; hàm ghép thao tác, đường dẫn và thông điệp hệ điều hành.
bool filesystemError(const std::error_code &ec,
                     const std::string &operation,
                     std::string &err) {
    if (!ec) return false;
    err = operation + ": " + ec.message();
    return true;
}

// Kiểm tra điều kiện của `isMissingPathError`.
bool isMissingPathError(const std::error_code &ec) noexcept {
    return ec == std::errc::no_such_file_or_directory ||
           ec == std::errc::not_a_directory;
}

// Chuyển nghiêm ngặt số nguyên; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
bool toStrictInt(const StackValue &value, int &out) {
    if (std::holds_alternative<int>(value)) {
        out = std::get<int>(value);
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        out = static_cast<int>(std::get<double>(value));
        return true;
    }
    if (!std::holds_alternative<std::string>(value)) return false;
    try {
        const std::string &text = std::get<std::string>(value);
        std::size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) return false;
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

// Chuyển nghiêm ngặt double; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
bool toStrictDouble(const StackValue &value, double &out) {
    if (std::holds_alternative<int>(value)) {
        out = static_cast<double>(std::get<int>(value));
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        out = std::get<double>(value);
        return true;
    }
    if (!std::holds_alternative<std::string>(value)) return false;
    try {
        const std::string &text = std::get<std::string>(value);
        std::size_t consumed = 0;
        const double parsed = std::stod(text, &consumed);
        if (consumed != text.size()) return false;
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

// Chạy kiểu tên; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
std::string runtimeTypeName(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return "số nguyên";
    if (std::holds_alternative<double>(value)) return "số thực";
    if (std::holds_alternative<std::string>(value)) return "chuỗi";
    if (std::holds_alternative<std::monostate>(value)) return "rỗng";
    if (std::holds_alternative<MapHandle>(value)) return "từ điển";
    if (std::holds_alternative<ListHandle>(value)) return "danh sách";
    if (std::holds_alternative<TupleHandle>(value)) return "tuple";
    if (std::holds_alternative<ClassHandle>(value)) return "lớp";
    if (std::holds_alternative<InstanceHandle>(value)) return "đối tượng";
    return "không rõ";
}

// Trả tên văn bản ổn định cho nền tảng; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
std::string platformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "không rõ";
#endif
}

} // namespace

// Dispatch nhóm hàm native nền tảng/thư viện chuẩn; handler kiểm tra tên hàm và thực hiện filesystem, time, environment hoặc utility tương ứng.
bool handleNativeFoundationFunction(const std::string &fn,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnToString)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(sv_to_string(args[0]));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnToInt)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        int parsed = 0;
        if (!toStrictInt(args[0], parsed)) {
            err = "thành số nguyên: giá trị không thể chuyển đổi";
            return true;
        }
        result = make_int_value(parsed);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnToFloat)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        double parsed = 0.0;
        if (!toStrictDouble(args[0], parsed)) {
            err = "thành số thực: giá trị không thể chuyển đổi";
            return true;
        }
        result = make_float_value(parsed);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnTypeOf)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(runtimeTypeName(args[0]));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnRandomInt)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        int minimum = 0;
        int maximum = 0;
        if (!toStrictInt(args[0], minimum) || !toStrictInt(args[1], maximum)) {
            err = "ngẫu nhiên nguyên: giới hạn phải là số nguyên";
            return true;
        }
        if (minimum > maximum) {
            err = "ngẫu nhiên nguyên: giới hạn dưới lớn hơn giới hạn trên";
            return true;
        }
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(minimum, maximum);
        result = make_int_value(distribution(generator));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathJoin)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        result = make_string_value(pathToUtf8(utf8Path(args[0]) / utf8Path(args[1])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathName)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(pathToUtf8(utf8Path(args[0]).filename()));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathParent)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(pathToUtf8(utf8Path(args[0]).parent_path()));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathExists) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathIsFile) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathIsDirectory)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        bool value = false;
        const fs::path path = utf8Path(args[0]);
        if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathExists)) {
            value = fs::exists(path, ec);
        } else if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathIsFile)) {
            value = fs::is_regular_file(path, ec);
        } else {
            value = fs::is_directory(path, ec);
        }
        // Query predicates have boolean semantics: a missing path is simply
        // false. Windows reports ENOENT through error_code for some of these
        // overloads while POSIX implementations commonly return false with a
        // clear error_code, so normalize that platform difference here.
        if (isMissingPathError(ec)) {
            ec.clear();
            value = false;
        }
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(value ? 1 : 0);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnCreateDirectory)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        const fs::path path = utf8Path(args[0]);
        fs::create_directories(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(fs::is_directory(path, ec) ? 1 : 0);
        if (filesystemError(ec, fn, err)) return true;
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListDirectory)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        const fs::path path = utf8Path(args[0]);
        std::vector<std::string> names;
        fs::directory_iterator iterator(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        for (const auto &entry : iterator) {
            names.push_back(pathToUtf8(entry.path().filename()));
        }
        std::sort(names.begin(), names.end());
        std::vector<StackValue> values;
        values.reserve(names.size());
        for (const std::string &name : names) values.push_back(make_string_value(name));
        result = make_list_value(std::move(values));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnRemovePath)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        const auto removed = fs::remove_all(utf8Path(args[0]), ec);
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(static_cast<int>(removed));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnEnvGet)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        const std::string name = argToRawString(args[0]);
        const auto value = getEnvVar(name.c_str());
        result = make_string_value(value.has_value() ? *value : argToRawString(args[1]));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPlatformName)) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        result = make_string_value(platformName());
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSleepMs)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        int milliseconds = 0;
        if (!toStrictInt(args[0], milliseconds) || milliseconds < 0) {
            err = "ngủ mili giây: thời lượng phải là số nguyên không âm";
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        result = make_null_value();
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
