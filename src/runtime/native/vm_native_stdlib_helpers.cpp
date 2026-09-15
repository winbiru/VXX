#include "common/vm_native_stdlib_helpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"

namespace vietvm::helpers {

// Chuyển chuỗi UTF-8 thành `std::filesystem::path` ngay tại native boundary.
// Runtime chỉ nhận đường dẫn UTF-8 hợp lệ để Windows/Unix không diễn giải cùng
// một chuỗi đầu vào theo hai cách khác nhau.
bool nativeUtf8Path(const StackValue &value,
                    const std::string &operation,
                    std::filesystem::path &path,
                    std::string &err) {
    const std::string text = argToRawString(value);
    if (!vietvm::core::isValidUtf8(text)) {
        err = operation + ": đường dẫn UTF-8 không hợp lệ";
        return false;
    }
    path = std::filesystem::u8path(text);
    return true;
}

namespace {

namespace fs = std::filesystem;

// Chuyển path hệ điều hành về UTF-8 với separator `/`. Trên POSIX tên file có
// thể chứa byte không phải UTF-8; không để dữ liệu đó lọt ngược vào string V++.
bool pathToUtf8(const fs::path &path,
                const std::string &operation,
                std::string &text,
                std::string &err) {
    text = path.generic_u8string();
    if (!vietvm::core::isValidUtf8(text)) {
        err = operation + ": hệ thống tệp trả về đường dẫn không phải UTF-8";
        return false;
    }
    return true;
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
        const double number = std::get<double>(value);
        if (!std::isfinite(number) || std::trunc(number) != number ||
            number < static_cast<double>(std::numeric_limits<int>::min()) ||
            number > static_cast<double>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(number);
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

// Tên biến môi trường đi qua native boundary phải có cùng contract trên mọi nền tảng.
// `getenv`/`_dupenv_s` không thống nhất cách xử lý tên rỗng, dấu `=` hoặc NUL nhúng,
// vì vậy V++ từ chối các trường hợp đó trước khi gọi CRT/POSIX.
bool validateEnvironmentVariableName(const std::string &name,
                                     const std::string &operation,
                                     std::string &err) {
    if (name.empty() || name.find('=') != std::string::npos ||
        name.find('\0') != std::string::npos) {
        err = operation + ": tên biến môi trường không hợp lệ";
        return false;
    }
    if (!vietvm::core::isValidUtf8(name)) {
        err = operation + ": tên biến môi trường phải là UTF-8 hợp lệ";
        return false;
    }
    return true;
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
    if (std::holds_alternative<ClosureHandle>(value)) return "hàm";
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

bool localTime(std::time_t value, std::tm &out) {
#if defined(_WIN32)
    return localtime_s(&out, &value) == 0;
#else
    return localtime_r(&value, &out) != nullptr;
#endif
}

bool utcTime(std::time_t value, std::tm &out) {
#if defined(_WIN32)
    return gmtime_s(&out, &value) == 0;
#else
    return gmtime_r(&value, &out) != nullptr;
#endif
}

bool timezoneOffsetMinutes(std::time_t now,
                           const std::tm &local,
                           const std::tm &utc,
                           int &minutes) {
#if defined(_WIN32)
    std::tm localCopy = local;
    const __time64_t localAsUtc = _mkgmtime64(&localCopy);
    if (localAsUtc == -1) return false;
    minutes = static_cast<int>((localAsUtc - static_cast<__time64_t>(now)) / 60);
    return true;
#elif defined(__APPLE__) || defined(__linux__)
    (void)now;
    (void)utc;
    minutes = static_cast<int>(local.tm_gmtoff / 60);
    return true;
#else
    std::tm localCopy = local;
    std::tm utcCopy = utc;
    utcCopy.tm_isdst = -1;
    const std::time_t localEpoch = std::mktime(&localCopy);
    const std::time_t utcAsLocalEpoch = std::mktime(&utcCopy);
    if (localEpoch == static_cast<std::time_t>(-1) ||
        utcAsLocalEpoch == static_cast<std::time_t>(-1)) {
        return false;
    }
    minutes = static_cast<int>(std::difftime(localEpoch, utcAsLocalEpoch) / 60.0);
    return true;
#endif
}

bool formatUtcIso8601(std::time_t now, std::string &out) {
    std::tm utc{};
    if (!utcTime(now, utc)) return false;
    char buffer[32] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return false;
    }
    out = buffer;
    return true;
}

bool formatLocalIso8601(std::time_t now, std::string &out, int &offsetMinutes) {
    std::tm local{};
    std::tm utc{};
    if (!localTime(now, local) || !utcTime(now, utc) ||
        !timezoneOffsetMinutes(now, local, utc, offsetMinutes)) {
        return false;
    }

    char dateTime[32] = {0};
    if (std::strftime(dateTime, sizeof(dateTime), "%Y-%m-%dT%H:%M:%S", &local) == 0) {
        return false;
    }

    const char sign = offsetMinutes < 0 ? '-' : '+';
    const int absoluteMinutes = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    char offset[8] = {0};
    if (std::snprintf(offset, sizeof(offset), "%c%02d:%02d", sign,
                      absoluteMinutes / 60, absoluteMinutes % 60) <= 0) {
        return false;
    }
    out = std::string(dateTime) + offset;
    return true;
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
        fs::path root;
        fs::path child;
        if (!nativeUtf8Path(args[0], fn, root, err) ||
            !nativeUtf8Path(args[1], fn, child, err)) return true;
        std::string joined;
        if (!pathToUtf8(root / child, fn, joined, err)) return true;
        result = make_string_value(joined);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathName)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        std::string name;
        if (!pathToUtf8(path.filename(), fn, name, err)) return true;
        result = make_string_value(name);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathParent)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        std::string parent;
        if (!pathToUtf8(path.parent_path(), fn, parent, err)) return true;
        result = make_string_value(parent);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathExists) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathIsFile) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPathIsDirectory)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        bool value = false;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
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
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        fs::create_directories(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(fs::is_directory(path, ec) ? 1 : 0);
        if (filesystemError(ec, fn, err)) return true;
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnListDirectory)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        std::vector<std::string> names;
        fs::directory_iterator iterator(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        for (const auto &entry : iterator) {
            std::string name;
            if (!pathToUtf8(entry.path().filename(), fn, name, err)) return true;
            names.push_back(std::move(name));
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
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        const auto removed = fs::remove_all(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(static_cast<int>(removed));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnEnvGet)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        const std::string name = argToRawString(args[0]);
        if (!validateEnvironmentVariableName(name, fn, err)) return true;
        const auto value = getEnvVar(name.c_str());
        result = make_string_value(value.has_value() ? *value : argToRawString(args[1]));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnPlatformName)) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        result = make_string_value(platformName());
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnNow)) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        const std::time_t now = std::time(nullptr);
        int offsetMinutes = 0;
        std::string formatted;
        if (!formatLocalIso8601(now, formatted, offsetMinutes)) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeTimeFormatFailed, {fn});
            return true;
        }
        result = make_string_value(formatted);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnNowUtc)) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        std::string formatted;
        if (!formatUtcIso8601(std::time(nullptr), formatted)) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeTimeFormatFailed, {fn});
            return true;
        }
        result = make_string_value(formatted);
        return true;
    }

    if (vietvm::constants::matchesAnyName(
            fn, vietvm::constants::kFnTimezoneOffsetMinutes)) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        std::tm utc{};
        int offsetMinutes = 0;
        if (!localTime(now, local) || !utcTime(now, utc) ||
            !timezoneOffsetMinutes(now, local, utc, offsetMinutes)) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeTimeFormatFailed, {fn});
            return true;
        }
        result = make_int_value(offsetMinutes);
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
