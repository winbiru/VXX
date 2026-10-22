#include "vpp/runtime/path_machine.h"

#include <climits>
#include <string>
#include "vpp/core/text.h"
#include "vpp/runtime/error.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#if defined(VPP_FILE_MACHINE_ASM)
extern "C" int vpp_machine_path_stat(const char *, struct stat *) noexcept;
extern "C" int vpp_machine_path_lstat(const char *, struct stat *) noexcept;
#endif
#endif

namespace vietvm::runtime {
namespace {
RuntimeError fail(const std::string &message) {
    return RuntimeError("máy đường dẫn: " + message, RuntimeErrorKind::CallBoundary);
}
#if defined(_WIN32)
std::wstring widePath(const std::string &path) {
    if (path.size() > INT_MAX) throw fail("đường dẫn quá dài");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
        static_cast<int>(path.size()), nullptr, 0);
    if (n <= 0) throw fail("không thể chuyển UTF-8 sang UTF-16");
    std::wstring result(static_cast<std::size_t>(n), 0);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
            static_cast<int>(path.size()), result.data(), n) != n)
        throw fail("không thể chuyển đường dẫn");
    return result;
}
#endif
}

bool isPathMachinePrimitive(int opcode) noexcept {
    return opcode == OP_VM_MACHINE_PATH_KIND;
}

StackValue executePathMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (opcode != OP_VM_MACHINE_PATH_KIND) throw fail("opcode không hợp lệ");
    if (capabilities.count("machine.path.kind") == 0)
        throw fail("thiếu quyền machine.path.kind");
    if (arguments.size() != 2) throw fail("cần đường dẫn và chế độ theo liên kết");
    const auto *path = std::get_if<std::string>(&arguments[0]);
    const auto *follow = std::get_if<int>(&arguments[1]);
    if (!path || path->find('\0') != std::string::npos || !core::isValidUtf8(*path))
        throw fail("đường dẫn phải là UTF-8 không chứa NUL");
    if (!follow || (*follow != 0 && *follow != 1))
        throw fail("theo liên kết phải là 0 hoặc 1");
    if (path->empty()) return make_int_value(-1);
#if defined(_WIN32)
    const auto wide = widePath(*path);
    const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS |
        (*follow == 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0);
    HANDLE handle = CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return make_int_value(-1);
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(handle, &info);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!ok) throw fail("không thể đọc thuộc tính Windows, error=" + std::to_string(error));
    const DWORD attrs = info.dwFileAttributes;
    const int kind = *follow == 0 && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        ? 0 : (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 2
        : (attrs & FILE_ATTRIBUTE_DEVICE) ? 0 : 1;
    return make_int_value(kind);
#else
    struct stat info{};
#if defined(VPP_FILE_MACHINE_ASM)
    const int rc = *follow ? vpp_machine_path_stat(path->c_str(), &info)
                           : vpp_machine_path_lstat(path->c_str(), &info);
#else
    const int rc = *follow ? ::stat(path->c_str(), &info) : ::lstat(path->c_str(), &info);
#endif
    return make_int_value(rc == 0 ? static_cast<int>((info.st_mode & S_IFMT) >> 12) : -1);
#endif
}
} // namespace vietvm::runtime
