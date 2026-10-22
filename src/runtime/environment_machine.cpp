#include "vpp/runtime/environment_machine.h"

#include <climits>
#include <cstddef>
#include <string>

#include "vpp/runtime/error.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(VPP_FILE_MACHINE_ASM) && (defined(__APPLE__) || defined(__linux__))
extern "C" const char *vpp_machine_env_getenv(const char *) noexcept;
#else
#include <cstdlib>
#endif

namespace vietvm::runtime {
namespace {

#if defined(_WIN32)
std::wstring environmentWide(const std::string &utf8) {
    if (utf8.size() > static_cast<std::size_t>(INT_MAX)) {
        throw RuntimeError("máy môi trường: tên quá dài", RuntimeErrorKind::CallBoundary);
    }
    const int count = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0) {
        throw RuntimeError("máy môi trường: tên UTF-8 không hợp lệ", RuntimeErrorKind::CallBoundary);
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                              static_cast<int>(utf8.size()), wide.data(), count) != count) {
        throw RuntimeError("máy môi trường: chuyển UTF-8 thất bại", RuntimeErrorKind::CallBoundary);
    }
    return wide;
}

std::string environmentUtf8(const std::wstring &wide) {
    if (wide.empty()) return "";
    const int size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw RuntimeError("máy môi trường: giá trị UTF-16 không hợp lệ",
                           RuntimeErrorKind::CallBoundary);
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), static_cast<int>(wide.size()), bytes.data(), size, nullptr, nullptr) != size) {
        throw RuntimeError("máy môi trường: chuyển UTF-16 thất bại",
                           RuntimeErrorKind::CallBoundary);
    }
    return bytes;
}
#endif

StackValue readEnvironment(const std::string &name) {
#if defined(_WIN32)
    const auto wide = environmentWide(name);
    ::SetLastError(ERROR_SUCCESS);
    const DWORD required = ::GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
    if (required == 0) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND) return make_null_value();
        if (error == ERROR_SUCCESS) return make_string_value("");
        throw RuntimeError("máy môi trường: GetEnvironmentVariableW thất bại",
                           RuntimeErrorKind::CallBoundary);
    }
    std::wstring buffer(static_cast<std::size_t>(required), L'\0');
    ::SetLastError(ERROR_SUCCESS);
    const DWORD copied = ::GetEnvironmentVariableW(wide.c_str(), buffer.data(), required);
    if (copied >= required || (copied == 0 && ::GetLastError() != ERROR_SUCCESS)) {
        throw RuntimeError("máy môi trường: biến thay đổi trong lúc đọc",
                           RuntimeErrorKind::CallBoundary);
    }
    buffer.resize(copied);
    return make_string_value(environmentUtf8(buffer));
#elif defined(VPP_FILE_MACHINE_ASM) && (defined(__APPLE__) || defined(__linux__))
    // POSIX has no kernel getenv syscall: supported libc symbol is called
    // directly from Assembly. It remains a documented dynamic dependency.
    const char *value = vpp_machine_env_getenv(name.c_str());
    return value ? make_string_value(value) : make_null_value();
#else
    const char *value = std::getenv(name.c_str());
    return value ? make_string_value(value) : make_null_value();
#endif
}
} // namespace

bool isEnvironmentMachinePrimitive(int opcode) noexcept {
    return opcode == OP_VM_MACHINE_ENV_GET;
}

StackValue executeEnvironmentMachinePrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (!isEnvironmentMachinePrimitive(opcode)) {
        throw RuntimeError("máy môi trường: opcode không hợp lệ", RuntimeErrorKind::CallBoundary);
    }
    if (capabilities.count("system.env.read") == 0) {
        throw RuntimeError("máy môi trường: thiếu quyền system.env.read",
                           RuntimeErrorKind::CallBoundary);
    }
    if (arguments.size() != 1) {
        throw RuntimeError("máy môi trường: cần đúng một tên biến",
                           RuntimeErrorKind::CallBoundary);
    }
    const auto *name = std::get_if<std::string>(&arguments[0]);
    if (!name || name->empty() || name->find('=') != std::string::npos ||
        name->find('\0') != std::string::npos) {
        throw RuntimeError("máy môi trường: tên biến không hợp lệ",
                           RuntimeErrorKind::CallBoundary);
    }
    return readEnvironment(*name);
}

} // namespace vietvm::runtime
