#include "vpp/runtime/directory_machine.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include "vpp/core/text.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/resource_token.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#if defined(__linux__)
#include <fcntl.h>
#else
#include <dirent.h>
#endif
#include "vpp/runtime/file_machine.h"
#if defined(VPP_FILE_MACHINE_ASM) && !defined(__linux__)
extern "C" DIR *vpp_machine_dir_open(const char *) noexcept;
extern "C" dirent *vpp_machine_dir_next(DIR *) noexcept;
extern "C" int vpp_machine_dir_close(DIR *) noexcept;
#endif
#endif

namespace vietvm::runtime {
namespace {
RuntimeError fail(const std::string &what) {
    return RuntimeError("máy thư mục: " + what, RuntimeErrorKind::CallBoundary);
}
StackValue pair(int first, int second) {
    return make_list_value({make_int_value(first), make_int_value(second)});
}
StackValue result(int rc, int err) {
    return make_list_value({make_int_value(rc),
                            make_int_value(err == 0 ? 0 : err == EEXIST ? 1 : err == ENOENT ? 2 : 3),
                            make_int_value(err)});
}
StackValue entry(int state, std::string name = {}) {
    return make_list_value({make_int_value(state), make_string_value(std::move(name))});
}
const std::string &pathArg(const StackValue &value) {
    const auto *path = std::get_if<std::string>(&value);
    if (!path || path->empty() || path->find('\0') != std::string::npos ||
        !core::isValidUtf8(*path))
        throw fail("đường dẫn phải là UTF-8 không rỗng và không chứa NUL");
    return *path;
}
int tokenArg(const StackValue &value) {
    const auto *token = std::get_if<int>(&value);
    if (!token || *token <= 0) throw fail("token thư mục không hợp lệ");
    return *token;
}
#if defined(_WIN32)
int winError(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return ENOENT;
        case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return EEXIST;
        case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return EACCES;
        case ERROR_DIRECTORY: return ENOTDIR;
        case ERROR_INVALID_NAME: case ERROR_INVALID_PARAMETER: return EINVAL;
        default: return EIO;
    }
}
std::wstring widePath(const std::string &path) {
    if (path.size() > INT_MAX) throw fail("đường dẫn quá dài");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
        static_cast<int>(path.size()), nullptr, 0);
    if (n <= 0) throw fail("đường dẫn UTF-8 không hợp lệ");
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
            static_cast<int>(path.size()), wide.data(), n) != n)
        throw fail("không thể chuyển tên thư mục");
    return wide;
}
std::string utf8Name(const std::wstring &wide) {
    if (wide.size() > INT_MAX) throw fail("tên thư mục quá dài");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) throw fail("tên thư mục không thể mã hóa UTF-8");
    std::string name(static_cast<std::size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
            static_cast<int>(wide.size()), name.data(), n, nullptr, nullptr) != n)
        throw fail("không thể mã hóa tên thư mục");
    return name;
}
#endif
int release(DirectoryMachineState::Directory &directory) noexcept {
#if defined(_WIN32)
    if (directory.handle == 0) return 0; // empty directory
    return FindClose(reinterpret_cast<HANDLE>(directory.handle)) ? 0 : winError(GetLastError());
#elif defined(__linux__)
    if (directory.handle < 0) return 0;
    return file_machine::close(static_cast<int>(directory.handle)).error;
#else
    auto *handle = reinterpret_cast<DIR *>(directory.handle);
#if defined(VPP_FILE_MACHINE_ASM)
    const int rc = vpp_machine_dir_close(handle);
#else
    const int rc = ::closedir(handle);
#endif
    return rc == 0 ? 0 : errno;
#endif
}
} // namespace

bool isDirectoryMachinePrimitive(int opcode) noexcept {
    return opcode >= OP_VM_MACHINE_DIR_MKDIR && opcode <= OP_VM_MACHINE_DIR_CLOSE;
}

DirectoryMachineState::~DirectoryMachineState() { closeAll(); }
void DirectoryMachineState::closeAll() noexcept {
    for (auto &[token, directory] : directories) {
        (void)token;
        (void)release(directory);
    }
    directories.clear();
}

StackValue executeDirectoryMachinePrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities,
    DirectoryMachineState &state) {
    const char *capability = nullptr;
    switch (opcode) {
        case OP_VM_MACHINE_DIR_MKDIR: capability = "machine.dir.mkdir"; break;
        case OP_VM_MACHINE_DIR_REMOVE: capability = "machine.dir.remove"; break;
        case OP_VM_MACHINE_DIR_OPEN: capability = "machine.dir.open"; break;
        case OP_VM_MACHINE_DIR_NEXT: capability = "machine.dir.next"; break;
        case OP_VM_MACHINE_DIR_CLOSE: capability = "machine.dir.close"; break;
        default: throw fail("opcode không hợp lệ");
    }
    if (capabilities.count(capability) == 0) throw fail(std::string("thiếu quyền ") + capability);
    if (arguments.size() != 1) throw fail("sai số đối số primitive");

    if (opcode == OP_VM_MACHINE_DIR_MKDIR || opcode == OP_VM_MACHINE_DIR_REMOVE) {
        const auto &path = pathArg(arguments[0]);
        int error = 0;
#if defined(_WIN32)
        const auto wide = widePath(path);
        if (opcode == OP_VM_MACHINE_DIR_MKDIR) {
            if (!CreateDirectoryW(wide.c_str(), nullptr)) error = winError(GetLastError());
        } else {
            if (!DeleteFileW(wide.c_str())) {
                const DWORD failure = GetLastError();
                if ((failure != ERROR_ACCESS_DENIED && failure != ERROR_DIRECTORY) ||
                    RemoveDirectoryW(wide.c_str()) == 0)
                    error = winError(failure != ERROR_ACCESS_DENIED && failure != ERROR_DIRECTORY
                        ? failure : GetLastError());
            }
        }
#else
        auto operation = opcode == OP_VM_MACHINE_DIR_MKDIR
            ? file_machine::mkdir(path.c_str(), 0777)
            : file_machine::remove(path.c_str());
        if (opcode == OP_VM_MACHINE_DIR_REMOVE &&
            (operation.error == EISDIR || operation.error == EPERM))
            operation = file_machine::rmdir(path.c_str());
        error = operation.error;
#endif
        return result(error == 0 ? 0 : -1, error);
    }

    if (opcode == OP_VM_MACHINE_DIR_OPEN) {
        const auto &path = pathArg(arguments[0]);
        DirectoryMachineState::Directory directory;
#if defined(_WIN32)
        const auto wide = widePath(path);
        std::wstring pattern = wide;
        if (pattern.back() != L'/' && pattern.back() != L'\\') pattern += L'\\';
        pattern += L'*';
        WIN32_FIND_DATAW data{};
        HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            // FindFirstFileW reports FILE_NOT_FOUND for a valid empty folder.
            if (error != ERROR_FILE_NOT_FOUND) return pair(0, winError(error));
            const DWORD attr = GetFileAttributesW(wide.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
                return pair(0, attr == INVALID_FILE_ATTRIBUTES ? winError(GetLastError()) : ENOTDIR);
        } else {
            directory.handle = reinterpret_cast<std::intptr_t>(handle);
            directory.pending = true;
            directory.firstName = data.cFileName;
        }
#elif defined(__linux__)
        const auto opened = file_machine::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
        if (opened.error != 0) return pair(0, opened.error);
        directory.handle = opened.value;
#else
#if defined(VPP_FILE_MACHINE_ASM)
        DIR *handle = vpp_machine_dir_open(path.c_str());
#else
        DIR *handle = ::opendir(path.c_str());
#endif
        if (!handle) return pair(0, errno);
        directory.handle = reinterpret_cast<std::intptr_t>(handle);
#endif
        const auto token = nextOpaqueResourceToken();
        if (token > INT_MAX) {
            (void)release(directory);
            throw fail("hết định danh thư mục máy");
        }
        try {
            state.directories.emplace(static_cast<int>(token), std::move(directory));
        } catch (...) { (void)release(directory); throw; }
        return pair(static_cast<int>(token), 0);
    }

    const auto found = state.directories.find(tokenArg(arguments[0]));
    if (found == state.directories.end()) throw fail("token thư mục đã đóng hoặc không thuộc VM");
    auto &directory = found->second;
    if (opcode == OP_VM_MACHINE_DIR_CLOSE) {
        // Revoke token before calling OS: even a failed close cannot be retried.
        auto closing = std::move(directory);
        state.directories.erase(found);
        const int error = release(closing);
        return pair(error == 0 ? 0 : -1, error);
    }
#if defined(_WIN32)
    if (directory.pending) {
        directory.pending = false;
        return entry(1, utf8Name(directory.firstName));
    }
    if (directory.handle == 0) return entry(0);
    WIN32_FIND_DATAW data{};
    if (!FindNextFileW(reinterpret_cast<HANDLE>(directory.handle), &data)) {
        const DWORD error = GetLastError();
        return entry(error == ERROR_NO_MORE_FILES ? 0 : -winError(error));
    }
    return entry(1, utf8Name(data.cFileName));
#elif defined(__linux__)
    // getdents64 emits variable-length records with 19-byte headers. Decode
    // only complete records; a malformed result never reaches V++ as a name.
    for (;;) {
        if (directory.nextOffset == directory.bytesUsed) {
            const auto read = file_machine::directoryEntries(
                static_cast<int>(directory.handle), directory.buffer.data(), directory.buffer.size());
            if (read.error == EINTR) continue;
            if (read.error != 0) return entry(-read.error);
            if (read.value == 0) return entry(0);
            if (read.value < 0 || static_cast<std::size_t>(read.value) > directory.buffer.size())
                return entry(-EIO);
            directory.bytesUsed = static_cast<std::size_t>(read.value);
            directory.nextOffset = 0;
        }
        const auto remaining = directory.bytesUsed - directory.nextOffset;
        if (remaining < 20) return entry(-EIO);
        const char *record = directory.buffer.data() + directory.nextOffset;
        std::uint16_t recordLength = 0;
        std::memcpy(&recordLength, record + 16, sizeof(recordLength));
        if (recordLength < 20 || recordLength > remaining) return entry(-EIO);
        const char *nameStart = record + 19;
        const auto nameLength = static_cast<std::size_t>(recordLength) - 19;
        const char *terminator = static_cast<const char *>(std::memchr(nameStart, 0, nameLength));
        if (!terminator) return entry(-EIO);
        directory.nextOffset += recordLength;
        const std::string name(nameStart, terminator);
        if (!core::isValidUtf8(name)) throw fail("tên thư mục không phải UTF-8");
        return entry(1, name);
    }
#else
    errno = 0; // readdir returns null both for EOF and for an OS error.
#if defined(VPP_FILE_MACHINE_ASM)
    auto *item = vpp_machine_dir_next(reinterpret_cast<DIR *>(directory.handle));
#else
    auto *item = ::readdir(reinterpret_cast<DIR *>(directory.handle));
#endif
    if (!item) {
        const int error = errno;
        return entry(error == 0 ? 0 : -error);
    }
    const std::string name(item->d_name);
    if (!core::isValidUtf8(name)) throw fail("tên thư mục không phải UTF-8");
    return entry(1, name);
#endif
}
} // namespace vietvm::runtime
