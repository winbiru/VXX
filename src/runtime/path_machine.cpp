#include "vpp/runtime/path_machine.h"

#include <climits>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>
#include "vpp/core/text.h"
#include "vpp/core/project_layout.h"
#include "vpp/runtime/error.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
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
StackValue missingFile() {
    return make_list_value({make_string_value("missing")});
}
StackValue fileStatError(std::uint64_t code) {
    // OS failures cross the VM boundary as data; the V++ library raises a
    // catchable language exception while cache policy stays in V++.
    return make_list_value({make_string_value("error"),make_string_value(std::to_string(code))});
}
StackValue regularFile(std::uint64_t size, std::int64_t seconds, std::int64_t nanoseconds) {
    // Metadata remains strings across the VM boundary: a V++ integer may be
    // narrower than off_t. Keeping seconds and nanos separate avoids signed
    // nanosecond overflow for historical/far-future timestamps.
    return make_list_value({make_string_value("present"),
        make_string_value(std::to_string(size)),
        make_string_value(std::to_string(seconds)),
        make_string_value(std::to_string(nanoseconds))});
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
    return opcode == OP_VM_MACHINE_PATH_KIND || opcode == OP_VM_MACHINE_PATH_STAT ||
        (opcode >= OP_VM_MACHINE_PATH_ABSOLUTE && opcode <= OP_VM_MACHINE_PATH_EXECUTABLE);
}

StackValue executePathMachinePrimitive(Opcode opcode,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (!isPathMachinePrimitive(opcode)) throw fail("opcode không hợp lệ");
    if (capabilities.count("machine.path.kind") == 0)
        throw fail("thiếu quyền machine.path.kind");
    if (opcode >= OP_VM_MACHINE_PATH_ABSOLUTE && opcode <= OP_VM_MACHINE_PATH_EXECUTABLE) {
        namespace fs = std::filesystem;
        const std::size_t arity = opcode == OP_VM_MACHINE_PATH_CURRENT || opcode == OP_VM_MACHINE_PATH_TEMP
            ? 0 : opcode == OP_VM_MACHINE_PATH_EQUIVALENT ? 2 : 1;
        if (arguments.size() != arity) throw fail("số đối số đường dẫn không hợp lệ");
        if (opcode == OP_VM_MACHINE_PATH_SET_CURRENT && !capabilities.count("machine.path.chdir"))
            throw fail("thiếu quyền machine.path.chdir");
        for (const auto &argument : arguments) {
            const auto *path = std::get_if<std::string>(&argument);
            if (!path || path->find('\0') != std::string::npos || !core::isValidUtf8(*path))
                throw fail("đường dẫn phải là UTF-8 không chứa NUL");
        }
        const auto path = [&](std::size_t i) { return core::utf8Path(std::get<std::string>(arguments[i])); };
        std::error_code error;
        StackValue result = make_int_value(0);
        switch (opcode) {
            case OP_VM_MACHINE_PATH_ABSOLUTE:
                result = fs::absolute(path(0), error).u8string(); break;
            case OP_VM_MACHINE_PATH_CANONICAL: {
                const auto absolute = fs::absolute(path(0), error);
                if (!error) result = fs::weakly_canonical(absolute, error).u8string();
                break;
            }
            case OP_VM_MACHINE_PATH_EQUIVALENT:
                result = make_int_value(fs::equivalent(path(0), path(1), error)); break;
            case OP_VM_MACHINE_PATH_CURRENT:
                result = fs::current_path(error).u8string(); break;
            case OP_VM_MACHINE_PATH_SET_CURRENT:
                fs::current_path(path(0), error); break;
            case OP_VM_MACHINE_PATH_TEMP:
                result = fs::temp_directory_path(error).u8string(); break;
            case OP_VM_MACHINE_PATH_EXECUTABLE:
#if defined(_WIN32)
                result = make_int_value(fs::is_regular_file(path(0), error));
#else
                result = make_int_value(::access(path(0).c_str(), X_OK) == 0);
#endif
                break;
            default: throw fail("opcode không hợp lệ");
        }
        if (error) return fileStatError(static_cast<std::uint64_t>(error.value()));
        return make_list_value({make_string_value("ok"), std::move(result)});
    }
    const bool statFile = opcode == OP_VM_MACHINE_PATH_STAT;
    if (arguments.size() != (statFile ? 1U : 2U))
        throw fail("số đối số đường dẫn không hợp lệ");
    const auto *path = std::get_if<std::string>(&arguments[0]);
    const int alwaysFollow = 1;
    const auto *follow = statFile ? &alwaysFollow : std::get_if<int>(&arguments[1]);
    if (!path || path->find('\0') != std::string::npos || !core::isValidUtf8(*path))
        throw fail("đường dẫn phải là UTF-8 không chứa NUL");
    if (!follow || (*follow != 0 && *follow != 1))
        throw fail("theo liên kết phải là 0 hoặc 1");
    if (path->empty()) return statFile ? missingFile() : make_int_value(-1);
#if defined(_WIN32)
    const auto wide = widePath(*path);
    const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS |
        (*follow == 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0);
    HANDLE handle = CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (!statFile) return make_int_value(-1);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return missingFile();
        return fileStatError(error);
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(handle, &info);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!ok) {
        if (statFile) return fileStatError(error);
        throw fail("không thể đọc thuộc tính Windows, error=" + std::to_string(error));
    }
    const DWORD attrs = info.dwFileAttributes;
    if (statFile) {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_DEVICE))
            return missingFile();
        const std::uint64_t size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32)
            | info.nFileSizeLow;
        const std::uint64_t ticks = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32)
            | info.ftLastWriteTime.dwLowDateTime;
        // FILETIME is 100ns since 1601-01-01; normalize to Unix nanoseconds.
        const auto seconds = static_cast<std::int64_t>(ticks / 10000000ULL) - 11644473600LL;
        const auto nanos = static_cast<std::int64_t>(ticks % 10000000ULL) * 100;
        return regularFile(size, seconds, nanos);
    }
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
    if (statFile) {
        if (rc != 0) {
#if defined(VPP_FILE_MACHINE_ASM) && defined(__linux__)
            // Linux ASM uses raw newfstatat: failures return -errno without
            // updating libc errno. A stale errno must never turn ENOENT into
            // a genuine OS failure (or hide ENAMETOOLONG as a cache miss).
            const int error = rc < 0 ? -rc : errno;
#else
            const int error = errno;
#endif
            if (error == ENOENT || error == ENOTDIR) return missingFile();
            return fileStatError(static_cast<std::uint64_t>(error));
        }
        if (!S_ISREG(info.st_mode)) return missingFile();
#if defined(__APPLE__)
        return regularFile(static_cast<std::uint64_t>(info.st_size),
            static_cast<std::int64_t>(info.st_mtimespec.tv_sec),
            static_cast<std::int64_t>(info.st_mtimespec.tv_nsec));
#else
        return regularFile(static_cast<std::uint64_t>(info.st_size),
            static_cast<std::int64_t>(info.st_mtim.tv_sec),
            static_cast<std::int64_t>(info.st_mtim.tv_nsec));
#endif
    }
    return make_int_value(rc == 0 ? static_cast<int>((info.st_mode & S_IFMT) >> 12) : -1);
#endif
}
} // namespace vietvm::runtime
