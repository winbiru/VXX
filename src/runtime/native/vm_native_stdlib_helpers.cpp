#include "common/vm_native_stdlib_helpers.h"
#include "vpp/bytecode/intrinsic.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "common/vm_native_constants.h"
#include "common/vm_native_helpers.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#elif defined(__linux__)
#include <openssl/rand.h>
#endif

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
        err = messages::formatMessage(messages::kNativePathUtf8Invalid, {operation});
        return false;
    }
    path = std::filesystem::u8path(text);
    return true;
}

namespace {

namespace fs = std::filesystem;
constexpr int kMaxSecureRandomBytes = 4096;

int foldPublicHash(std::uint32_t hash) noexcept {
    return static_cast<int>(hash & 0x7fffffffu);
}

template <typename Handle>
int identityHash(const Handle &handle) noexcept {
    if (handle == nullptr) return 0;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(handle.get());
    std::uint32_t folded = static_cast<std::uint32_t>(address);
    if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t)) {
        folded ^= static_cast<std::uint32_t>(address >> 32u);
    }
    folded ^= 0x9e3779b9u;
    folded *= 16777619u;
    return foldPublicHash(folded);
}

bool fillSecureRandom(std::vector<unsigned char> &bytes, std::string &err) {
    if (bytes.empty()) return true;
#if defined(_WIN32)
    const NTSTATUS status = BCryptGenRandom(
        nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        err = messages::messageText(messages::kNativeSecureRandomWindowsFailed);
        return false;
    }
    return true;
#elif defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, bytes.size(), bytes.data()) != errSecSuccess) {
        err = messages::messageText(messages::kNativeSecureRandomAppleFailed);
        return false;
    }
    return true;
#elif defined(__linux__)
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        err = messages::messageText(messages::kNativeSecureRandomLinuxFailed);
        return false;
    }
    return true;
#else
    err = messages::messageText(messages::kNativeSecureRandomPlatformUnsupported);
    return false;
#endif
}

// Chuyển path hệ điều hành về UTF-8 với separator `/`. Trên POSIX tên file có
// thể chứa byte không phải UTF-8; không để dữ liệu đó lọt ngược vào string V++.
bool pathToUtf8(const fs::path &path,
                const std::string &operation,
                std::string &text,
                std::string &err) {
    text = path.generic_u8string();
    if (!vietvm::core::isValidUtf8(text)) {
        err = messages::formatMessage(messages::kNativeFilesystemPathUtf8Invalid, {operation});
        return false;
    }
    return true;
}

// Chuyển `std::error_code` filesystem thành lỗi runtime có ngữ cảnh; hàm ghép thao tác, đường dẫn và thông điệp hệ điều hành.
bool filesystemError(const std::error_code &ec,
                     const std::string &operation,
                     std::string &err) {
    if (!ec) return false;
    err = messages::formatMessage(messages::kNativeOperationSystemError, {operation, ec.message()});
    return true;
}

// Kiểm tra điều kiện của `isMissingPathError`.
bool isMissingPathError(const std::error_code &ec) noexcept {
    return ec == std::errc::no_such_file_or_directory ||
           ec == std::errc::not_a_directory;
}

// Tên biến môi trường đi qua native boundary phải có cùng contract trên mọi nền tảng.
// `getenv`/`_dupenv_s` không thống nhất cách xử lý tên rỗng, dấu `=` hoặc NUL nhúng,
// vì vậy V++ từ chối các trường hợp đó trước khi gọi CRT/POSIX.
bool validateEnvironmentVariableName(const std::string &name,
                                     const std::string &operation,
                                     std::string &err) {
    if (name.empty() || name.find('=') != std::string::npos ||
        name.find('\0') != std::string::npos) {
        err = messages::formatMessage(messages::kNativeEnvironmentNameInvalid, {operation});
        return false;
    }
    if (!vietvm::core::isValidUtf8(name)) {
        err = messages::formatMessage(messages::kNativeEnvironmentNameUtf8Invalid, {operation});
        return false;
    }
    return true;
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

} // namespace

// Dispatch nhóm hàm native nền tảng/thư viện chuẩn; handler kiểm tra tên hàm và thực hiện filesystem, time, environment hoặc utility tương ứng.
bool handleNativeFoundationFunction(Opcode opcode,
                                    const std::vector<StackValue> &args,
                                    StackValue &result,
                                    std::string &err) {
    const auto *primitive = vietvm::bytecode::intrinsicByOpcode(opcode);
    if (primitive == nullptr) return false;
    const std::string fn(primitive->name);

    if (opcode == OP_VM_TYPE_OF) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        result = make_string_value(runtimeTypeName(args[0]));
        return true;
    }

    if (opcode == OP_VM_IDENTITY_HASH) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (std::holds_alternative<MapHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<MapHandle>(args[0])));
        } else if (std::holds_alternative<ListHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<ListHandle>(args[0])));
        } else if (std::holds_alternative<TupleHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<TupleHandle>(args[0])));
        } else if (std::holds_alternative<ClassHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<ClassHandle>(args[0])));
        } else if (std::holds_alternative<InstanceHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<InstanceHandle>(args[0])));
        } else if (std::holds_alternative<ClosureHandle>(args[0])) {
            result = make_int_value(identityHash(std::get<ClosureHandle>(args[0])));
        } else {
            err = fn + ": chỉ nhận giá trị tham chiếu của VM";
        }
        return true;
    }

    if (opcode == OP_VM_NGAU_NHIEN_BAO_MAT_BYTES) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (!std::holds_alternative<int>(args[0])) {
            err = messages::messageText(messages::kNativeSecureRandomByteCountInvalid);
            return true;
        }
        const int byteCount = std::get<int>(args[0]);
        if (byteCount < 1 || byteCount > kMaxSecureRandomBytes) {
            err = messages::messageText(messages::kNativeSecureRandomByteCountInvalid);
            return true;
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(byteCount));
        if (!fillSecureRandom(bytes, err)) return true;
        std::vector<StackValue> values;
        values.reserve(bytes.size());
        for (unsigned char byte : bytes) {
            values.push_back(make_int_value(static_cast<int>(byte)));
        }
        result = make_list_value(std::move(values));
        return true;
    }

    if (opcode == OP_VM_DUONG_DAN_TON_TAI || opcode == OP_VM_LA_TEP ||
        opcode == OP_VM_LA_THU_MUC) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        bool value = false;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        if (opcode == OP_VM_DUONG_DAN_TON_TAI) {
            value = fs::exists(path, ec);
        } else if (opcode == OP_VM_LA_TEP) {
            value = fs::is_regular_file(path, ec);
        } else {
            value = fs::is_directory(path, ec);
        }
        if (isMissingPathError(ec)) {
            ec.clear();
            value = false;
        }
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(value ? 1 : 0);
        return true;
    }

    if (opcode == OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        std::error_code ec;
        const auto status = fs::symlink_status(path, ec);
        if (isMissingPathError(ec)) {
            result = make_int_value(-1);
            return true;
        }
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(fs::is_directory(status) ? 1 : 0);
        return true;
    }

    if (opcode == OP_VM_TAO_THU_MUC) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        // Primitive thấp: chỉ tạo đúng một directory entry. Việc tìm cha và
        // tạo cả cây thuộc gói/nhập xuất/thư mục.vi.
        const bool created = fs::create_directory(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(created ? 1 : 0);
        return true;
    }

    if (opcode == OP_VM_LIET_KE_THU_MUC) {
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
        std::vector<StackValue> values;
        values.reserve(names.size());
        for (const std::string &name : names) values.push_back(make_string_value(name));
        result = make_list_value(std::move(values));
        return true;
    }

    if (opcode == OP_VM_XOA_DUONG_DAN) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::error_code ec;
        fs::path path;
        if (!nativeUtf8Path(args[0], fn, path, err)) return true;
        // Primitive thấp: unlink/rmdir đúng một entry. Traversal và recursion
        // nằm hoàn toàn ở thư viện V++.
        const bool removed = fs::remove(path, ec);
        if (filesystemError(ec, fn, err)) return true;
        result = make_int_value(removed ? 1 : 0);
        return true;
    }

    if (opcode == OP_VM_DOC_BIEN_MOI_TRUONG) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        const std::string name = argToRawString(args[0]);
        if (!validateEnvironmentVariableName(name, fn, err)) return true;
        const auto value = getEnvVar(name.c_str());
        result = value.has_value() ? make_string_value(*value) : make_null_value();
        return true;
    }

    if (opcode == OP_VM_TEN_NEN_TANG) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        result = make_string_value(platformName());
        return true;
    }

    // One clock read supplies both calendar fields and the local UTC offset.
    // ISO-8601 formatting belongs to gói/thời gian, not the OS boundary.
    if (opcode == OP_VM_DONG_HO_DIA_PHUONG || opcode == OP_VM_DONG_HO_UTC) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        const bool utcMode = opcode == OP_VM_DONG_HO_UTC;
        const std::time_t now = std::time(nullptr);
        std::tm calendar{};
        std::tm utc{};
        int offsetMinutes = 0;
        bool ok = false;
        if (utcMode) {
            ok = utcTime(now, calendar);
        } else {
            ok = localTime(now, calendar) && utcTime(now, utc) &&
                 timezoneOffsetMinutes(now, calendar, utc, offsetMinutes);
        }
        if (!ok) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeTimeFormatFailed, {fn});
            return true;
        }
        result = make_list_value({
            make_int_value(calendar.tm_year + 1900),
            make_int_value(calendar.tm_mon + 1),
            make_int_value(calendar.tm_mday),
            make_int_value(calendar.tm_hour),
            make_int_value(calendar.tm_min),
            make_int_value(calendar.tm_sec),
            make_int_value(offsetMinutes),
        });
        return true;
    }

    if (opcode == OP_VM_THOI_GIAN_DON_DIEU_MS) {
        if (!requireNativeArgumentCount(args, fn, 0, err)) return true;
        const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
        result = make_float_value(
            std::chrono::duration<double, std::milli>(elapsed).count());
        return true;
    }

    if (opcode == OP_VM_NGU_MILI_GIAY) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        if (!std::holds_alternative<int>(args[0])) {
            err = messages::messageText(messages::kNativeSleepMillisecondsInvalid);
            return true;
        }
        const int milliseconds = std::get<int>(args[0]);
        if (milliseconds < 0) {
            err = messages::messageText(messages::kNativeSleepMillisecondsInvalid);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        result = make_null_value();
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
