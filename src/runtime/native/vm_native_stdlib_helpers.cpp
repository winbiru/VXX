#include "common/vm_native_stdlib_helpers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
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

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <Security/Security.h>
#elif defined(__linux__)
#include <openssl/evp.h>
#include <openssl/hmac.h>
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
        err = operation + ": đường dẫn UTF-8 không hợp lệ";
        return false;
    }
    path = std::filesystem::u8path(text);
    return true;
}

namespace {

namespace fs = std::filesystem;
constexpr std::size_t kSha256DigestSize = 32;
constexpr int kMaxSecureRandomBytes = 4096;

std::string bytesToLowerHex(const unsigned char *bytes, std::size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        result[i * 2] = kHex[(bytes[i] >> 4) & 0x0f];
        result[i * 2 + 1] = kHex[bytes[i] & 0x0f];
    }
    return result;
}

bool requireUtf8CryptoString(const std::vector<StackValue> &args,
                             std::size_t index,
                             const std::string &fn,
                             const char *label,
                             std::string &value,
                             std::string &err) {
    if (index >= args.size() || !std::holds_alternative<std::string>(args[index])) {
        err = fn + ": " + label + " phải là chuỗi";
        return false;
    }
    value = std::get<std::string>(args[index]);
    if (!vietvm::core::isValidUtf8(value)) {
        err = fn + ": " + label + " phải là UTF-8 hợp lệ";
        return false;
    }
    return true;
}

bool fillSecureRandom(std::vector<unsigned char> &bytes, std::string &err) {
    if (bytes.empty()) return true;
#if defined(_WIN32)
    const NTSTATUS status = BCryptGenRandom(
        nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        err = "ngẫu nhiên bảo mật: BCryptGenRandom thất bại";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, bytes.size(), bytes.data()) != errSecSuccess) {
        err = "ngẫu nhiên bảo mật: SecRandomCopyBytes thất bại";
        return false;
    }
    return true;
#elif defined(__linux__)
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        err = "ngẫu nhiên bảo mật: RAND_bytes thất bại";
        return false;
    }
    return true;
#else
    err = "ngẫu nhiên bảo mật: nền tảng chưa được hỗ trợ";
    return false;
#endif
}

bool sha256Digest(const std::string &text,
                  std::array<unsigned char, kSha256DigestSize> &digest,
                  std::string &err) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD copied = 0;
    std::vector<unsigned char> object;

    auto closeHandles = [&]() {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    };

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) {
        status = BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &copied, 0);
    }
    if (status >= 0) {
        status = BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &copied, 0);
    }
    if (status >= 0 && hashLength != digest.size()) {
        closeHandles();
        err = "băm sha256: BCrypt trả kích thước digest không hợp lệ";
        return false;
    }
    if (status >= 0) {
        object.resize(objectLength);
        status = BCryptCreateHash(
            algorithm, &hash, object.data(), objectLength, nullptr, 0, 0);
    }
    if (status >= 0 && !text.empty()) {
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
            closeHandles();
            err = "băm sha256: dữ liệu quá lớn";
            return false;
        } else {
            status = BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(const_cast<char *>(text.data())),
                static_cast<ULONG>(text.size()), 0);
        }
    }
    if (status >= 0) {
        status = BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    }
    closeHandles();
    if (status < 0) {
        err = "băm sha256: BCrypt SHA-256 thất bại";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<CC_LONG>::max())) {
        err = "băm sha256: dữ liệu quá lớn";
        return false;
    }
    if (CC_SHA256(text.data(), static_cast<CC_LONG>(text.size()), digest.data()) == nullptr) {
        err = "băm sha256: CommonCrypto SHA-256 thất bại";
        return false;
    }
    return true;
#elif defined(__linux__)
    unsigned int digestSize = 0;
    if (EVP_Digest(text.data(), text.size(), digest.data(), &digestSize,
                   EVP_sha256(), nullptr) != 1 ||
        digestSize != digest.size()) {
        err = "băm sha256: OpenSSL SHA-256 thất bại";
        return false;
    }
    return true;
#else
    (void)text;
    (void)digest;
    err = "băm sha256: nền tảng chưa được hỗ trợ";
    return false;
#endif
}

bool hmacSha256Digest(const std::string &key,
                      const std::string &text,
                      std::array<unsigned char, kSha256DigestSize> &digest,
                      std::string &err) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD copied = 0;
    std::vector<unsigned char> object;

    auto closeHandles = [&]() {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    };

    if (key.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)()) ||
        text.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
        err = "hmac sha256: dữ liệu quá lớn";
        return false;
    }

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0) {
        status = BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &copied, 0);
    }
    if (status >= 0) {
        status = BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &copied, 0);
    }
    if (status >= 0 && hashLength != digest.size()) {
        closeHandles();
        err = "hmac sha256: BCrypt trả kích thước digest không hợp lệ";
        return false;
    }
    if (status >= 0) {
        object.resize(objectLength);
        status = BCryptCreateHash(
            algorithm, &hash, object.data(), objectLength,
            reinterpret_cast<PUCHAR>(const_cast<char *>(key.data())),
            static_cast<ULONG>(key.size()), 0);
    }
    if (status >= 0 && !text.empty()) {
        status = BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(const_cast<char *>(text.data())),
            static_cast<ULONG>(text.size()), 0);
    }
    if (status >= 0) {
        status = BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    }
    closeHandles();
    if (status < 0) {
        err = "hmac sha256: BCrypt HMAC thất bại";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    (void)err;
    CCHmac(kCCHmacAlgSHA256, key.data(), key.size(),
           text.data(), text.size(), digest.data());
    return true;
#elif defined(__linux__)
    if (key.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        err = "hmac sha256: khóa quá lớn";
        return false;
    }
    unsigned int digestSize = 0;
    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char *>(text.data()), text.size(),
             digest.data(), &digestSize) == nullptr ||
        digestSize != digest.size()) {
        err = "hmac sha256: OpenSSL HMAC thất bại";
        return false;
    }
    return true;
#else
    (void)key;
    (void)text;
    (void)digest;
    err = "hmac sha256: nền tảng chưa được hỗ trợ";
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
            number < static_cast<double>((std::numeric_limits<int>::min)()) ||
            number > static_cast<double>((std::numeric_limits<int>::max)())) {
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

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSecureRandom)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        int byteCount = 0;
        if (!toStrictInt(args[0], byteCount) ||
            byteCount < 1 || byteCount > kMaxSecureRandomBytes) {
            err = "ngẫu nhiên bảo mật: số byte phải là số nguyên từ 1 đến 4096";
            return true;
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(byteCount));
        if (!fillSecureRandom(bytes, err)) return true;
        result = make_string_value(bytesToLowerHex(bytes.data(), bytes.size()));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnSha256)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::string text;
        if (!requireUtf8CryptoString(args, 0, fn, "văn bản", text, err)) return true;
        std::array<unsigned char, kSha256DigestSize> digest{};
        if (!sha256Digest(text, digest, err)) return true;
        result = make_string_value(bytesToLowerHex(digest.data(), digest.size()));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHmacSha256)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::string key;
        std::string text;
        if (!requireUtf8CryptoString(args, 0, fn, "khóa", key, err) ||
            !requireUtf8CryptoString(args, 1, fn, "văn bản", text, err)) {
            return true;
        }
        std::array<unsigned char, kSha256DigestSize> digest{};
        if (!hmacSha256Digest(key, text, digest, err)) return true;
        result = make_string_value(bytesToLowerHex(digest.data(), digest.size()));
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
