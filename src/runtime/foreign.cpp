#include "vpp/runtime/foreign.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <signal.h>
#include <string_view>
#include <time.h>
#include <cstring>
#include <climits>
#include <cwchar>

#include "vpp/runtime/error.h"
#include "common/vm_native_m3_helpers.h"

#if defined(VPP_HAS_LIBFFI)
#include <ffi.h>
#endif

#if !defined(_WIN32)
#include <dlfcn.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#endif

namespace vietvm::runtime {
namespace {

// Tokens are unique across VM instances and resets. A token issued to VM A
// must never accidentally match an unrelated open stream in VM B.
std::atomic<std::uint64_t> nextForeignResourceToken{1};
// Windows legacy native socket IDs use a separate range. POSIX sockets and
// their TLS sessions share one VM-owned token registry.
constexpr int foreignSocketBase = 1 << 29;
std::atomic<std::uint64_t> nextForeignSocketToken{foreignSocketBase};

RuntimeError ffiError(const std::string &message) {
    return RuntimeError("FFI: " + message, RuntimeErrorKind::CallBoundary);
}

#if defined(_WIN32)
std::wstring foreignWide(const std::string &utf8) {
    if (utf8.find('\0') != std::string::npos)
        throw ffiError("chuỗi FFI chứa NUL");
    if (utf8.empty()) return {};
    if (utf8.size() > INT_MAX)
        throw ffiError("chuỗi FFI quá dài");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n == 0) throw ffiError("chuỗi FFI không phải UTF-8 hợp lệ");
    std::wstring result(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8.data(), static_cast<int>(utf8.size()), result.data(), n) != n)
        throw ffiError("chuyển UTF-8 sang UTF-16 thất bại");
    return result;
}

std::string foreignUtf8(const std::wstring &wide) {
    if (wide.empty()) return {};
    if (wide.size() > INT_MAX) throw ffiError("chuỗi UTF-16 quá dài");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n == 0) throw ffiError("chuỗi UTF-16 không hợp lệ");
    std::string result(static_cast<std::size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide.data(), static_cast<int>(wide.size()), result.data(), n,
            nullptr, nullptr) != n)
        throw ffiError("chuyển UTF-16 sang UTF-8 thất bại");
    return result;
}

// Expose the same narrow errno contract to V++ on both supported hosts.
// Windows API error codes never escape into the POSIX-style FFI error slot.
int foreignWindowsPathErrno(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: return ENOENT;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS: return EEXIST;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION: return EACCES;
        case ERROR_DIRECTORY: return ENOTDIR;
        case ERROR_DIR_NOT_EMPTY: return ENOTEMPTY;
        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME: return EINVAL;
        default: return EIO;
    }
}

// Win32 search handles are owned by the VM, just like POSIX DIR* handles.
// The initial FindFirstFileW entry must be consumed before FindNextFileW.
struct ForeignWindowsDirectory {
    HANDLE search = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW entry{};
    bool firstPending = false;
    bool exhausted = false;
};

void closeForeignWindowsDirectory(ForeignWindowsDirectory *directory) noexcept {
    if (directory == nullptr) return;
    if (directory->search != INVALID_HANDLE_VALUE)
        (void)FindClose(directory->search);
    delete directory;
}

int foreignParentProcessId() {
    const DWORD self = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int parent = -1;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == self) {
                parent = static_cast<int>(entry.th32ParentProcessID);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return parent;
}

// Keep the portable V++ 16-byte little-endian seconds/nanoseconds contract.
void foreignTimespecBytes(std::vector<unsigned char> &buffer,
                          std::uint64_t seconds, std::uint64_t nanoseconds) {
    if (buffer.size() != 16) throw ffiError("clock_gettime yêu cầu đệm 16 byte");
    for (int i = 0; i < 8; ++i) {
        buffer[static_cast<std::size_t>(i)] =
            static_cast<unsigned char>(seconds >> (i * 8));
        buffer[static_cast<std::size_t>(8 + i)] =
            static_cast<unsigned char>(nanoseconds >> (i * 8));
    }
}
#endif

// Socket I/O can fail because the network is unavailable, the peer refuses a
// connection, or an OS policy denies bind/connect. These are application-level
// failures and must reach a V++ `bắt lỗi` handler. Descriptor/capability/ABI
// violations continue to use RuntimeError and remain VM faults.
[[noreturn]] void throwForeignSocketIoError(const std::string &message) {
    throw LanguageException(make_string_value("FFI: " + message));
}

#if !defined(_WIN32)
// The dynamic library must close even when argument validation or marshaling
// throws before ffi_call. Keep one owner for the entire call boundary.
struct ForeignLibraryGuard {
    void *handle = nullptr;
    ~ForeignLibraryGuard() {
        if (handle != nullptr) dlclose(handle);
    }
};

bool setForeignSocketTimeout(int fd, int milliseconds) {
    timeval value{};
    value.tv_sec = milliseconds / 1000;
    value.tv_usec = (milliseconds % 1000) * 1000;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) == 0 &&
           ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) == 0;
}

void suppressForeignSocketSigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
    (void)fd;
#endif
}

int registerForeignSocket(ForeignFileState &state, int fd, bool datagram,
                          bool listener) {
    const auto candidate = nextForeignSocketToken.fetch_add(1);
    if (candidate > INT32_MAX) {
        (void)::close(fd);
        throw ffiError("hết định danh socket POSIX");
    }
    try {
        state.sockets.emplace(static_cast<int>(candidate),
                              ForeignFileState::Socket{fd, datagram, listener});
    } catch (...) {
        (void)::close(fd);
        throw;
    }
    return static_cast<int>(candidate);
}

int connectForeignSocket(const std::string &host, int port, bool datagram,
                         int timeoutMs) {
    const std::string service = std::to_string(port);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = datagram ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = datagram ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo *head = nullptr;
    const int status = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &head);
    if (status != 0 || head == nullptr) {
        if (head != nullptr) ::freeaddrinfo(head);
        throw ffiError("socket_connect: địa chỉ IP không hợp lệ");
    }
    // The V++ resolver already provides one numeric address at a time and
    // retries other addresses. The native adapter makes only one OS attempt.
    const int fd = ::socket(head->ai_family, head->ai_socktype, head->ai_protocol);
    if (fd < 0) {
        const int error = errno;
        ::freeaddrinfo(head);
        throwForeignSocketIoError("socket_connect: kết nối thất bại, errno=" +
                                  std::to_string(error));
    }
    suppressForeignSocketSigpipe(fd);
    const int flags = ::fcntl(fd, F_GETFL, 0);
    int rc = flags < 0 ? -1 : ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (rc == 0) rc = ::connect(fd, head->ai_addr, head->ai_addrlen);
    if (rc != 0 && errno == EINPROGRESS) {
        // poll handles descriptors >= FD_SETSIZE. EINTR never resets deadline.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeoutMs);
        while (true) {
            const auto remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(deadline -
                                           std::chrono::steady_clock::now()).count();
            if (remaining <= 0) { errno = ETIMEDOUT; rc = -1; break; }
            pollfd writable{fd, POLLOUT, 0};
            rc = ::poll(&writable, 1, static_cast<int>(remaining));
            if (rc < 0 && errno == EINTR) continue;
            if (rc > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0) {
                    rc = error == 0 ? 0 : -1;
                    if (error != 0) errno = error;
                } else rc = -1;
            } else {
                if (rc == 0) errno = ETIMEDOUT;
                rc = -1;
            }
            break;
        }
    }
    const bool connected = rc == 0 && ::fcntl(fd, F_SETFL, flags) == 0 &&
                           setForeignSocketTimeout(fd, timeoutMs);
    const int lastError = errno;
    ::freeaddrinfo(head);
    if (!connected) {
        (void)::close(fd);
        throwForeignSocketIoError("socket_connect: kết nối thất bại, errno=" +
                                  std::to_string(lastError));
    }
    return fd;
}

int listenForeignSocket(int port, int backlog) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        throwForeignSocketIoError("socket_listen: không tạo được socket, errno=" +
                                  std::to_string(errno));
    }
    suppressForeignSocketSigpipe(fd);
    int reuse = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, backlog) != 0) {
        const int error = errno;
        (void)::close(fd);
        throwForeignSocketIoError("socket_listen: bind/listen thất bại, errno=" +
                                  std::to_string(error));
    }
    return fd;
}

void closeForeignProcess(ForeignFileState::Process &process) noexcept {
    if (process.stdoutFd >= 0) (void)::close(process.stdoutFd);
    if (process.stderrFd >= 0) (void)::close(process.stderrFd);
    process.stdoutFd = process.stderrFd = -1;
    if (process.pid > 0) {
        // Each child is spawned as the leader of its own process group. Kill
        // that group so timeout/VM reset also stops descendants which inherited
        // our pipe descriptors. Never signal the embedding VM's process group.
        (void)::kill(-static_cast<pid_t>(process.pid), SIGKILL);
        int status = 0;
        while (::waitpid(static_cast<pid_t>(process.pid), &status, 0) < 0 &&
               errno == EINTR) {}
        process.pid = -1;
    }
}

// argv and envp are transient contiguous C arrays owned until spawn returns.
// V++ adds one argument/environment entry at a time; no shell interpretation.
bool launchForeignProcess(ForeignFileState::Process &process) {
    int output[2] = {-1, -1};
    int errors[2] = {-1, -1};
    if (::pipe(output) != 0 || ::pipe(errors) != 0) {
        const int failure = errno;
        for (int fd : {output[0], output[1], errors[0], errors[1]}) {
            if (fd >= 0) (void)::close(fd);
        }
        process.error = std::strerror(failure);
        return false;
    }
    // If the embedding process has closed one of 0/1/2, pipe() can reuse it.
    // File-action addclose must never close the child's final stdout/stderr.
    for (int *fd : {&output[0], &output[1], &errors[0], &errors[1]}) {
        if (*fd >= STDERR_FILENO + 1) continue;
        const int promoted = ::fcntl(*fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        if (promoted < 0) {
            const int failure = errno;
            for (int toClose : {output[0], output[1], errors[0], errors[1]})
                (void)::close(toClose);
            process.error = std::strerror(failure);
            return false;
        }
        (void)::close(*fd);
        *fd = promoted;
    }
    // Stop concurrent children from inheriting the read ends. dup2 in the
    // spawn file actions explicitly makes STDOUT/STDERR inheritable.
    for (int fd : {output[0], output[1], errors[0], errors[1]}) {
        if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
            const int failure = errno;
            for (int toClose : {output[0], output[1], errors[0], errors[1]})
                (void)::close(toClose);
            process.error = std::strerror(failure);
            return false;
        }
    }
    posix_spawn_file_actions_t actions{};
    int rc = ::posix_spawn_file_actions_init(&actions);
    const bool actionsInitialized = rc == 0;
    if (rc == 0) rc = ::posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    if (rc == 0) rc = ::posix_spawn_file_actions_adddup2(&actions, errors[1], STDERR_FILENO);
    if (rc == 0) rc = ::posix_spawn_file_actions_addclose(&actions, output[0]);
    if (rc == 0) rc = ::posix_spawn_file_actions_addclose(&actions, errors[0]);
    if (rc == 0) rc = ::posix_spawn_file_actions_addclose(&actions, output[1]);
    if (rc == 0) rc = ::posix_spawn_file_actions_addclose(&actions, errors[1]);
    posix_spawnattr_t attributes{};
    bool attributesInitialized = false;
    if (rc == 0) {
        rc = ::posix_spawnattr_init(&attributes);
        attributesInitialized = rc == 0;
    }
    if (rc == 0) rc = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (rc == 0) rc = ::posix_spawnattr_setpgroup(&attributes, 0);
    if (rc == 0) {
        std::vector<char *> argv;
        argv.reserve(process.arguments.size() + 2);
        argv.push_back(process.program.data());
        for (auto &argument : process.arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);

        std::unordered_map<std::string, std::string> environment;
        for (char **entry = environ; entry && *entry; ++entry) {
            const std::string item(*entry);
            const auto separator = item.find('=');
            if (separator != std::string::npos)
                environment[item.substr(0, separator)] = item.substr(separator + 1);
        }
        for (const auto &[key, value] : process.environment) environment[key] = value;
        std::vector<std::string> storage;
        storage.reserve(environment.size());
        for (const auto &[key, value] : environment) storage.push_back(key + "=" + value);
        std::vector<char *> envp;
        envp.reserve(storage.size() + 1);
        for (auto &item : storage) envp.push_back(item.data());
        envp.push_back(nullptr);
        pid_t pid = -1;
        rc = ::posix_spawnp(&pid, process.program.c_str(), &actions, &attributes,
                            argv.data(), envp.data());
        if (rc == 0) process.pid = static_cast<int>(pid);
    }
    if (attributesInitialized) (void)::posix_spawnattr_destroy(&attributes);
    if (actionsInitialized) (void)::posix_spawn_file_actions_destroy(&actions);
    (void)::close(output[1]);
    (void)::close(errors[1]);
    if (rc != 0) {
        (void)::close(output[0]);
        (void)::close(errors[0]);
        process.error = std::strerror(rc);
        return false;
    }
    process.stdoutFd = output[0];
    process.stderrFd = errors[0];
    return true;
}
#endif

#if defined(_WIN32)
// Windows process handles are owned by one VM. The language builds argv/env,
// multiplexes the two pipes, applies deadlines and assembles the result.
struct ForeignWinHandle {
    HANDLE value = nullptr;
    explicit ForeignWinHandle(HANDLE handle = nullptr) : value(handle) {}
    ForeignWinHandle(const ForeignWinHandle &) = delete;
    ForeignWinHandle &operator=(const ForeignWinHandle &) = delete;
    ~ForeignWinHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

struct ForeignWindowsEnvLess {
    bool operator()(const std::wstring &left, const std::wstring &right) const {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    }
};

std::vector<wchar_t> foreignWindowsEnvironment(
    const std::unordered_map<std::string, std::string> &overrides) {
    const LPWCH original = GetEnvironmentStringsW();
    if (!original) throw ffiError("không đọc được môi trường Windows");
    std::map<std::wstring, std::wstring, ForeignWindowsEnvLess> environment;
    for (const wchar_t *entry = original; *entry; entry += std::wcslen(entry) + 1) {
        const std::wstring item(entry);
        const std::size_t separator = item.find(L'=', item[0] == L'=' ? 1 : 0);
        if (separator != std::wstring::npos)
            environment[item.substr(0, separator)] = item.substr(separator + 1);
    }
    FreeEnvironmentStringsW(original);
    for (const auto &[name, value] : overrides)
        environment[foreignWide(name)] = foreignWide(value);
    std::vector<wchar_t> block;
    for (const auto &[name, value] : environment) {
        block.insert(block.end(), name.begin(), name.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    if (block.size() == 1) block.push_back(L'\0');
    return block;
}

void closeForeignProcess(ForeignFileState::Process &process) noexcept {
    if (process.windowsStdout) CloseHandle(static_cast<HANDLE>(process.windowsStdout));
    if (process.windowsStderr) CloseHandle(static_cast<HANDLE>(process.windowsStderr));
    process.windowsStdout = process.windowsStderr = nullptr;
    // KILL_ON_JOB_CLOSE kills descendants as well as the direct child on
    // timeout or VM reset. Never leave a suspended/orphaned child behind.
    if (process.windowsJob) CloseHandle(static_cast<HANDLE>(process.windowsJob));
    process.windowsJob = nullptr;
    if (process.windowsProcess) {
        (void)WaitForSingleObject(static_cast<HANDLE>(process.windowsProcess), 5000);
        CloseHandle(static_cast<HANDLE>(process.windowsProcess));
    }
    process.windowsProcess = nullptr;
    process.pid = -1;
}

bool launchForeignProcess(ForeignFileState::Process &process) {
    // Quoting, argv assembly and escaping live in gói/hệ thống/tiến trình.vi.
    const std::wstring command = foreignWide(process.windowsCommandLine);
    const auto environment = foreignWindowsEnvironment(process.environment);
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE outReadRaw = nullptr, outWriteRaw = nullptr;
    HANDLE errReadRaw = nullptr, errWriteRaw = nullptr;
    // Construct each pipe independently so all ends are released on failure.
    if (!CreatePipe(&outReadRaw, &outWriteRaw, &security, 0)) {
        process.error = "CreatePipe stdout thất bại, mã " + std::to_string(GetLastError());
        return false;
    }
    ForeignWinHandle outRead(outReadRaw), outWrite(outWriteRaw);
    if (!CreatePipe(&errReadRaw, &errWriteRaw, &security, 0)) {
        process.error = "CreatePipe stderr thất bại, mã " + std::to_string(GetLastError());
        return false;
    }
    ForeignWinHandle errRead(errReadRaw), errWrite(errWriteRaw);
    if (!SetHandleInformation(outRead.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(errRead.value, HANDLE_FLAG_INHERIT, 0)) {
        process.error = "không đặt được pipe non-inheritable";
        return false;
    }
    ForeignWinHandle emptyStdin(CreateFileW(L"NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (emptyStdin.value == INVALID_HANDLE_VALUE) {
        process.error = "không mở được stdin rỗng";
        return false;
    }
    // Only the three intended standard handles can escape to the child.
    SIZE_T attributeBytes = 0;
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    if (!attributeBytes) {
        process.error = "không tạo được danh sách handle kế thừa";
        return false;
    }
    std::vector<unsigned char> attributeBuffer(attributeBytes);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
        process.error = "không khởi tạo được danh sách handle kế thừa";
        return false;
    }
    HANDLE inherited[] = {emptyStdin.value, outWrite.value, errWrite.value};
    const BOOL attributesValid = UpdateProcThreadAttribute(attributes, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr);
    if (!attributesValid) {
        DeleteProcThreadAttributeList(attributes);
        process.error = "không giới hạn được các handle kế thừa";
        return false;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = emptyStdin.value;
    startup.StartupInfo.hStdOutput = outWrite.value;
    startup.StartupInfo.hStdError = errWrite.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION started{};
    const BOOL created = CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
              EXTENDED_STARTUPINFO_PRESENT,
        const_cast<wchar_t *>(environment.data()), nullptr, &startup.StartupInfo, &started);
    const DWORD createError = created ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attributes);
    if (!created) {
        process.error = "CreateProcessW thất bại, mã " + std::to_string(createError);
        return false;
    }
    ForeignWinHandle child(started.hProcess), thread(started.hThread);
    ForeignWinHandle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const BOOL secured = job.value &&
        SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                &limits, sizeof(limits)) &&
        AssignProcessToJobObject(job.value, child.value);
    if (!secured || ResumeThread(thread.value) == DWORD(-1)) {
        const DWORD error = GetLastError();
        (void)TerminateProcess(child.value, 1);
        (void)WaitForSingleObject(child.value, INFINITE);
        process.error = "không bảo đảm được vòng đời tiến trình, mã " + std::to_string(error);
        return false;
    }
    process.pid = static_cast<int>(started.dwProcessId);
    process.windowsProcess = child.value; child.value = nullptr;
    process.windowsJob = job.value; job.value = nullptr;
    process.windowsStdout = outRead.value; outRead.value = nullptr;
    process.windowsStderr = errRead.value; errRead.value = nullptr;
    // RAII closes both write ends immediately so EOF is observable by V++.
    return true;
}

int pollForeignWindowsProcess(ForeignFileState::Process &process, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeoutMs);
    for (;;) {
        int ready = 0;
        int open = 0;
        const void *handles[] = {process.windowsStdout, process.windowsStderr};
        for (int i = 0; i < 2; ++i) {
            if (!handles[i]) continue;
            ++open;
            DWORD available = 0;
            if (PeekNamedPipe(static_cast<HANDLE>(const_cast<void *>(handles[i])),
                              nullptr, 0, nullptr, &available, nullptr)) {
                if (available > 0) ready |= (1 << i);
            } else {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                    ready |= (1 << i);
                else throwForeignSocketIoError("process_poll: mã " + std::to_string(error));
            }
        }
        if (open == 0) return 4;
        if (ready != 0) return ready;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return 0;
        Sleep(static_cast<DWORD>(std::min<std::int64_t>(left, 10)));
    }
}
#endif

// V++ int literals are int32. A decimal string is an explicit lossless way to
// supply larger ABI arguments until the language exposes boxed constructors.
template <typename T>
T parseAbiDecimal(const std::string &value, std::string_view typeName) {
    T parsed = 0;
    const char *begin = value.data();
    const char *end = begin + value.size();
    const auto status = std::from_chars(begin, end, parsed, 10);
    if (value.empty() || status.ec != std::errc{} || status.ptr != end) {
        throw ffiError(std::string(typeName) +
                       " yêu cầu chuỗi số thập phân trong miền ABI");
    }
    return parsed;
}

std::int64_t ffiSigned64(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return std::get<int>(value);
    if (std::holds_alternative<std::string>(value)) {
        return parseAbiDecimal<std::int64_t>(std::get<std::string>(value), "i64");
    }
    if (std::holds_alternative<AbiInteger>(value)) {
        const AbiInteger &integer = std::get<AbiInteger>(value);
        if (integer.isSigned) return integer.signedValue;
        if (integer.unsignedValue <=
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(integer.unsignedValue);
        }
    }
    throw ffiError("i64 yêu cầu số nguyên chính xác trong miền int64");
}

std::uint64_t ffiUnsigned64(const StackValue &value) {
    if (std::holds_alternative<int>(value) && std::get<int>(value) >= 0) {
        return static_cast<std::uint64_t>(std::get<int>(value));
    }
    if (std::holds_alternative<std::string>(value)) {
        return parseAbiDecimal<std::uint64_t>(std::get<std::string>(value), "u64");
    }
    if (std::holds_alternative<AbiInteger>(value)) {
        const AbiInteger &integer = std::get<AbiInteger>(value);
        if (!integer.isSigned) return integer.unsignedValue;
        if (integer.signedValue >= 0) {
            return static_cast<std::uint64_t>(integer.signedValue);
        }
    }
    throw ffiError("u64 yêu cầu số nguyên không âm trong miền uint64");
}

std::uint32_t ffiUnsigned32(const StackValue &value) {
    std::uint64_t number = 0;
    if (std::holds_alternative<int>(value)) {
        const int input = std::get<int>(value);
        if (input < 0) throw ffiError("u32 yêu cầu số nguyên không âm");
        number = static_cast<std::uint64_t>(input);
    } else if (std::holds_alternative<std::string>(value)) {
        number = parseAbiDecimal<std::uint64_t>(std::get<std::string>(value), "u32");
    } else if (std::holds_alternative<AbiInteger>(value)) {
        const AbiInteger &integer = std::get<AbiInteger>(value);
        if (integer.isSigned) {
            if (integer.signedValue < 0) {
                throw ffiError("u32 yêu cầu số nguyên không âm");
            }
            number = static_cast<std::uint64_t>(integer.signedValue);
        } else {
            number = integer.unsignedValue;
        }
    } else {
        throw ffiError("u32 yêu cầu số nguyên chính xác");
    }
    if (number > std::numeric_limits<std::uint32_t>::max()) {
        throw ffiError("u32 vượt miền uint32");
    }
    return static_cast<std::uint32_t>(number);
}

#if defined(VPP_HAS_LIBFFI)
ffi_type *ffiType(vietvm::bytecode::ForeignAbiType type) {
    using vietvm::bytecode::ForeignAbiType;
    switch (type) {
        case ForeignAbiType::Void: return &ffi_type_void;
        case ForeignAbiType::I32: return &ffi_type_sint32;
        case ForeignAbiType::U32: return &ffi_type_uint32;
        case ForeignAbiType::I64: return &ffi_type_sint64;
        case ForeignAbiType::U64: return &ffi_type_uint64;
        case ForeignAbiType::F64: return &ffi_type_double;
        case ForeignAbiType::CString: return &ffi_type_pointer;
        case ForeignAbiType::BufferOut: return &ffi_type_pointer;
        case ForeignAbiType::BufferIn: return &ffi_type_pointer;
        case ForeignAbiType::FileHandle: return &ffi_type_pointer;
        case ForeignAbiType::DirectoryHandle: return &ffi_type_pointer;
        case ForeignAbiType::DirectoryEntry: return &ffi_type_pointer;
        case ForeignAbiType::PointerStatus: return &ffi_type_pointer;
        case ForeignAbiType::DnsHandle: return &ffi_type_pointer;
        case ForeignAbiType::SocketHandle: return &ffi_type_sint32;
    }
    return nullptr;
}

struct ArgumentStorage {
    std::int32_t i32 = 0;
    std::uint32_t u32 = 0;
    std::int64_t i64 = 0;
    std::uint64_t u64 = 0;
    double f64 = 0.0;
    const char *pointer = nullptr;
    int fileToken = 0;
    int directoryToken = 0;
    int socketToken = 0;
    std::vector<unsigned char> buffer;
};

union ReturnStorage {
    std::int32_t i32;
    std::uint32_t u32;
    std::int64_t i64;
    std::uint64_t u64;
    double f64;
    void *pointer;
    ReturnStorage() : pointer(nullptr) {}
};
#endif

} // namespace

ForeignFileState::~ForeignFileState() { closeAll(); }

void ForeignFileState::closeAll() noexcept {
    for (const auto &[id, file] : files) {
        (void)id;
        if (file != nullptr) std::fclose(file);
    }
    files.clear();
#if !defined(_WIN32)
    for (const auto &[id, directory] : directories) {
        (void)id;
        if (directory != nullptr) (void)::closedir(static_cast<DIR *>(directory));
    }
#else
    for (const auto &[id, directory] : directories) {
        (void)id;
        closeForeignWindowsDirectory(static_cast<ForeignWindowsDirectory *>(directory));
    }
#endif
    directories.clear();
#if !defined(_WIN32)
    for (const auto &[id, resolver] : resolvers) {
        (void)id;
        if (resolver.head != nullptr) ::freeaddrinfo(static_cast<addrinfo *>(resolver.head));
    }
#endif
    resolvers.clear();
#if !defined(_WIN32)
    for (const auto &[id, socket] : sockets) {
        (void)id;
        if (socket.tlsSession != nullptr)
            helpers::closeForeignTlsClient(socket.tlsSession);
        else if (socket.descriptor >= 0) (void)::close(socket.descriptor);
    }
#endif
    sockets.clear();
    for (auto &[id, process] : processes) {
        (void)id;
        closeForeignProcess(process);
    }
    processes.clear();
}

ForeignCallResult executeForeignCall(
    const vietvm::bytecode::ForeignFunctionDescriptor &descriptor,
    const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &grantedCapabilities,
    int previousPosixError,
    ForeignFileState *fileState) {
    if (descriptor.capability.empty() ||
        grantedCapabilities.find(descriptor.capability) ==
            grantedCapabilities.end()) {
        throw ffiError("thiếu khả năng: " + descriptor.capability);
    }
    // A host grant to read environment variables must not authorize an
    // arbitrary libc symbol such as system(3). Bind the narrow grant to its
    // complete ABI contract before resolving or invoking a native symbol.
    if (descriptor.capability == "system.env.read" &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "getenv" ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::CString} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::CString)) {
        throw ffiError("khả năng system.env.read chỉ cho phép getenv(c_chuỗi):c_chuỗi");
    }
    // The process identity grant is deliberately read-only and cannot be
    // repurposed to call another libc function (or change its ABI).
    if (descriptor.capability == "system.process.id" &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "getpid" ||
         descriptor.abi != "c" ||
         !descriptor.parameters.empty() ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("khả năng system.process.id chỉ cho phép getpid():i32");
    }
    // Parent-process introspection has a distinct, read-only grant. A caller
    // holding it cannot substitute another libc symbol or calling signature.
    if (descriptor.capability == "system.process.parent_id" &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "getppid" ||
         descriptor.abi != "c" ||
         !descriptor.parameters.empty() ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("khả năng system.process.parent_id chỉ cho phép getppid():i32");
    }
    // Read-only POSIX credentials have independent grants. uid_t/gid_t are
    // represented as u32 by this target slice; values outside V++ int32
    // are rejected by the common unsigned-return marshaller below.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4>
        identityBindings = {{{"system.user.id", "getuid"},
                             {"system.user.effective_id", "geteuid"},
                             {"system.group.id", "getgid"},
                             {"system.group.effective_id", "getegid"}}};
    for (const auto &[capability, symbol] : identityBindings) {
        if (descriptor.capability != capability) continue;
        if (descriptor.library != "system.c" ||
            descriptor.symbol != symbol ||
            descriptor.abi != "c" ||
            !descriptor.parameters.empty() ||
            descriptor.result != vietvm::bytecode::ForeignAbiType::U32) {
            throw ffiError("khả năng " + descriptor.capability +
                           " chỉ cho phép " + std::string(symbol) + "():u32");
        }
    }
    // Sleep is a separate host permission, restricted to a single fixed-ABI
    // POSIX function. Never allow it to authorize arbitrary libc calls.
    if (descriptor.capability == "system.time.sleep" &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "usleep" ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::U32} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("khả năng system.time.sleep chỉ cho phép usleep(u32):i32");
    }
    // Two single-entry filesystem operations. The V++ library owns recursive
    // traversal; one grant must not be reusable for an unrelated libc symbol.
    if (descriptor.capability == "system.fs.mkdir" &&
        (descriptor.library != "system.c" || descriptor.symbol != "mkdir" ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::CString,
             vietvm::bytecode::ForeignAbiType::U32} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("system.fs.mkdir chỉ cho phép mkdir(c_chuỗi,u32):i32");
    }
    if (descriptor.capability == "system.fs.remove" &&
        (descriptor.library != "system.c" || descriptor.symbol != "remove" ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::CString} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("system.fs.remove chỉ cho phép remove(c_chuỗi):i32");
    }
    // F_OK checks existence while following symlinks. Access to a path is a
    // distinct read-only permission, never a grant for chmod/unlink or R_OK.
    if (descriptor.capability == "system.fs.exists" &&
        (descriptor.library != "system.c" || descriptor.symbol != "access" ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::CString,
             vietvm::bytecode::ForeignAbiType::I32} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("system.fs.exists chỉ cho phép access(c_chuỗi,i32):i32");
    }
    // Windows needs a small OS metadata query instead of the POSIX stat
    // layout. V++ owns the public exists/file/directory/no-follow policy.
    const bool windowsPathKind = descriptor.capability == "system.fs.path.kind";
    if (windowsPathKind &&
        (descriptor.library != "system.path" || descriptor.symbol != "kind" ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::CString,
             vietvm::bytecode::ForeignAbiType::I32} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("system.fs.path.kind chỉ cho phép kind(c_chuỗi,i32):i32");
    }
#if !defined(_WIN32)
    if (windowsPathKind)
        throw ffiError("system.fs.path.kind chỉ hỗ trợ Win32");
#endif
    // stat and lstat use target-specific struct layouts; keep the native
    // buffer contract narrow and publish only its probed st_mode offset.
    const bool statFollow = descriptor.capability == "system.fs.stat";
    const bool statNoFollow = descriptor.capability == "system.fs.lstat";
    if ((statFollow || statNoFollow) &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != (statFollow ? "stat" : "lstat") ||
         descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::CString,
             vietvm::bytecode::ForeignAbiType::BufferOut} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("system.fs.stat/lstat chỉ cho phép ABI stat(c_chuỗi,c_đệm_ra):i32");
    }
    const bool dirOpen = descriptor.capability == "system.fs.dir.open";
    const bool dirRead = descriptor.capability == "system.fs.dir.read";
    const bool dirClose = descriptor.capability == "system.fs.dir.close";
    if (dirOpen || dirRead || dirClose) {
        const std::vector<vietvm::bytecode::ForeignAbiType> expected = dirOpen
            ? std::vector<vietvm::bytecode::ForeignAbiType>{
                vietvm::bytecode::ForeignAbiType::CString}
            : std::vector<vietvm::bytecode::ForeignAbiType>{
                vietvm::bytecode::ForeignAbiType::DirectoryHandle};
        const auto result = dirOpen ? vietvm::bytecode::ForeignAbiType::DirectoryHandle
            : dirRead ? vietvm::bytecode::ForeignAbiType::DirectoryEntry
                      : vietvm::bytecode::ForeignAbiType::I32;
        if (descriptor.library != "system.c" || descriptor.abi != "c" ||
            descriptor.symbol != (dirOpen ? "opendir" : dirRead ? "readdir" : "closedir") ||
            descriptor.parameters != expected || descriptor.result != result ||
            fileState == nullptr) {
            throw ffiError("system.fs.dir yêu cầu binding POSIX DIR* đúng ABI và trạng thái VM");
        }
    }
    // Native FILE* is kept in a per-VM owner registry. Only five audited
    // stdio bindings may receive/produce opaque file handles or byte buffers.
    using vietvm::bytecode::ForeignAbiType;
    const bool fileOpen = descriptor.capability == "system.file.open";
    const bool fileRead = descriptor.capability == "system.file.read";
    const bool fileWrite = descriptor.capability == "system.file.write";
    const bool fileClose = descriptor.capability == "system.file.close";
    const bool fileError = descriptor.capability == "system.file.error";
    const bool fileBinding = fileOpen || fileRead || fileWrite || fileClose || fileError;
    if (fileBinding) {
        const std::vector<ForeignAbiType> expected = fileOpen
            ? std::vector<ForeignAbiType>{ForeignAbiType::CString, ForeignAbiType::CString}
            : fileRead ? std::vector<ForeignAbiType>{ForeignAbiType::BufferOut,
                ForeignAbiType::U64, ForeignAbiType::U64, ForeignAbiType::FileHandle}
            : fileWrite ? std::vector<ForeignAbiType>{ForeignAbiType::BufferIn,
                ForeignAbiType::U64, ForeignAbiType::U64, ForeignAbiType::FileHandle}
            : std::vector<ForeignAbiType>{ForeignAbiType::FileHandle};
        const std::string_view symbol = fileOpen ? "fopen" : fileRead ? "fread"
            : fileWrite ? "fwrite" : fileClose ? "fclose" : "ferror";
        const ForeignAbiType result = fileOpen ? ForeignAbiType::FileHandle
            : fileRead || fileWrite ? ForeignAbiType::U64 : ForeignAbiType::I32;
        if (descriptor.library != "system.c" || descriptor.abi != "c" ||
            descriptor.symbol != symbol || descriptor.parameters != expected ||
            descriptor.result != result || fileState == nullptr) {
            throw ffiError("system.file yêu cầu binding stdio đúng ABI và trạng thái VM");
        }
    }
    // A resolver handle is VM-owned. This is an audited OS adapter rather than
    // pretending getaddrinfo's addrinfo** is a portable flat libffi argument.
    // No native pointer or sockaddr structure is returned to V++.
    const bool dnsOpen = descriptor.capability == "system.net.resolve.open";
    const bool dnsNext = descriptor.capability == "system.net.resolve.next";
    const bool dnsClose = descriptor.capability == "system.net.resolve.close";
    if (dnsOpen || dnsNext || dnsClose) {
        const auto expectedParams = dnsOpen
            ? std::vector<ForeignAbiType>{ForeignAbiType::CString,
                ForeignAbiType::CString, ForeignAbiType::I32}
            : std::vector<ForeignAbiType>{ForeignAbiType::DnsHandle};
        const auto expectedResult = dnsOpen ? ForeignAbiType::DnsHandle
            : dnsNext ? ForeignAbiType::CString : ForeignAbiType::I32;
        if (descriptor.library != "system.net" || descriptor.abi != "c" ||
            descriptor.symbol != (dnsOpen ? "resolve_open" :
                                  dnsNext ? "resolve_next" : "resolve_close") ||
            descriptor.parameters != expectedParams ||
            descriptor.result != expectedResult || fileState == nullptr) {
            throw ffiError("system.net.resolve yêu cầu chữ ký và quyền resolver được kiểm chứng");
        }
    }
    // Process operations are audited adapters: libc's argv**/envp** and
    // posix_spawn_file_actions_t cannot be modelled as raw scalar libffi args.
    // Each operation has its own capability and exact ABI contract.
    const std::string_view processOp = descriptor.symbol;
    const bool processBinding = descriptor.library == "system.process";
    if (!processBinding &&
        descriptor.capability.compare(0, 15, "system.process.") == 0 &&
        descriptor.capability != "system.process.id" &&
        descriptor.capability != "system.process.parent_id") {
        throw ffiError("system.process chỉ cho phép binding adapter VM đã kiểm chứng");
    }
    if (processBinding) {
        using vietvm::bytecode::ForeignAbiType;
        const std::vector<ForeignAbiType> one{ForeignAbiType::I32};
        const std::vector<ForeignAbiType> pair{ForeignAbiType::I32,
                                                ForeignAbiType::CString};
        const std::vector<ForeignAbiType> environment{ForeignAbiType::I32,
            ForeignAbiType::CString, ForeignAbiType::CString};
        const std::vector<ForeignAbiType> poll{ForeignAbiType::I32,
            ForeignAbiType::I32};
        const std::vector<ForeignAbiType> read{ForeignAbiType::I32,
            ForeignAbiType::I32, ForeignAbiType::BufferOut, ForeignAbiType::U64};
        const std::vector<ForeignAbiType> expected = processOp == "new" ?
            std::vector<ForeignAbiType>{ForeignAbiType::CString} :
            processOp == "arg" || processOp == "command_line" ? pair :
            processOp == "env" ? environment :
            processOp == "poll" ? poll : processOp == "read" ? read : one;
        const ForeignAbiType output = processOp == "error"
            ? ForeignAbiType::CString : ForeignAbiType::I32;
        const bool recognized = processOp == "new" || processOp == "arg" ||
            processOp == "command_line" ||
            processOp == "env" || processOp == "start" || processOp == "poll" ||
            processOp == "read" || processOp == "wait" ||
            processOp == "error" || processOp == "close";
        if (!recognized || descriptor.capability !=
                "system.process." + std::string(processOp) ||
            descriptor.abi != "c" || descriptor.parameters != expected ||
            descriptor.result != output || fileState == nullptr) {
            throw ffiError("system.process yêu cầu chữ ký và quyền riêng từng thao tác");
        }
    }
    // Socket IDs are VM-owned, and distinct from the remaining legacy TLS IDs.
    // The adapter accepts numeric addresses only; hostname DNS resolution and
    // per-address retry remain in the V++ socket library.
    const bool sockConnect = descriptor.capability == "system.net.socket.connect";
    const bool sockListen = descriptor.capability == "system.net.socket.listen";
    const bool sockAccept = descriptor.capability == "system.net.socket.accept";
    const bool sockTimeout = descriptor.capability == "system.net.socket.timeout";
    const bool sockKind = descriptor.capability == "system.net.socket.kind";
    const bool sockSend = descriptor.capability == "system.net.socket.send";
    const bool sockRecv = descriptor.capability == "system.net.socket.recv";
    const bool sockClose = descriptor.capability == "system.net.socket.close";
    const bool tlsUpgrade = descriptor.capability == "system.net.tls.upgrade";
    const bool tlsSend = descriptor.capability == "system.net.tls.send";
    const bool tlsRecv = descriptor.capability == "system.net.tls.recv";
    const bool tlsBinding = tlsUpgrade || tlsSend || tlsRecv;
    const bool sockBinding = sockConnect || sockListen || sockAccept || sockTimeout ||
        sockKind || sockSend || sockRecv || sockClose;
    if (sockBinding) {
        const auto expected = sockConnect
            ? std::vector<ForeignAbiType>{ForeignAbiType::CString,
                ForeignAbiType::I32, ForeignAbiType::I32, ForeignAbiType::I32}
            : sockListen ? std::vector<ForeignAbiType>{ForeignAbiType::I32,
                ForeignAbiType::I32}
            : sockTimeout ? std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle,
                ForeignAbiType::I32}
            : sockSend ? std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle,
                ForeignAbiType::BufferIn, ForeignAbiType::U64, ForeignAbiType::I32}
            : sockRecv ? std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle,
                ForeignAbiType::BufferOut, ForeignAbiType::U64, ForeignAbiType::I32}
            : std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle};
        const auto expectedResult = sockConnect || sockListen || sockAccept
            ? ForeignAbiType::SocketHandle
            : sockSend || sockRecv ? ForeignAbiType::I64 : ForeignAbiType::I32;
        const std::string_view expectedSymbol = sockConnect ? "socket_connect"
            : sockListen ? "socket_listen" : sockAccept ? "socket_accept"
            : sockTimeout ? "socket_timeout" : sockKind ? "socket_kind"
            : sockSend ? "send" : sockRecv ? "recv" : "close";
        const std::string_view expectedLibrary = sockSend || sockRecv || sockClose
            ? "system.c" : "system.net";
        if (descriptor.library != expectedLibrary || descriptor.symbol != expectedSymbol ||
            descriptor.abi != "c" || descriptor.parameters != expected ||
            descriptor.result != expectedResult || fileState == nullptr) {
            throw ffiError("system.net.socket yêu cầu chữ ký và quyền socket đúng ABI");
        }
    }
    if (tlsBinding) {
        const auto expected = tlsUpgrade
            ? std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle,
                ForeignAbiType::CString}
            : std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle,
                tlsSend ? ForeignAbiType::BufferIn : ForeignAbiType::BufferOut,
                ForeignAbiType::U64};
        const auto output = tlsUpgrade ? ForeignAbiType::SocketHandle
                                       : ForeignAbiType::I64;
        const std::string_view symbol = tlsUpgrade ? "tls_upgrade"
            : tlsSend ? "tls_send" : "tls_recv";
        if (descriptor.library != "system.net" || descriptor.symbol != symbol ||
            descriptor.abi != "c" || descriptor.parameters != expected ||
            descriptor.result != output || fileState == nullptr) {
            throw ffiError("system.net.tls yêu cầu chữ ký và quyền TLS đúng ABI");
        }
    }
    // clock_gettime writes a native struct timespec. Only the two audited
    // clock IDs may receive a writable 16-byte buffer, under separate grants.
    const bool monotonicClock = descriptor.capability == "system.time.monotonic";
    const bool realtimeClock = descriptor.capability == "system.time.realtime";
    // getentropy(void *, size_t) only writes at most 256 bytes and reports
    // failure atomically. Bind the buffer length to the second ABI argument;
    // never expose an arbitrary writable pointer under this grant.
    const bool entropyRead = descriptor.capability == "system.entropy.read";
    // The native tm pointer returned by localtime_r is never exposed to V++.
    // Its sole permitted representation is a 0/1 success status; V++ reads
    // copied struct bytes after the call. Both buffers are ABI-checked below.
    const bool localClock = descriptor.capability == "system.time.local";
    if (localClock &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "localtime_r" || descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::BufferIn,
             vietvm::bytecode::ForeignAbiType::BufferOut} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::PointerStatus)) {
        throw ffiError("system.time.local chỉ cho phép localtime_r(c_đệm_vào,c_đệm_ra):c_cờ_con_trỏ");
    }
    if (entropyRead &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "getentropy" || descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::BufferOut,
             vietvm::bytecode::ForeignAbiType::U64} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError("system.entropy.read chỉ cho phép getentropy(c_đệm_ra,u64):i32");
    }
    if ((monotonicClock || realtimeClock) &&
        (descriptor.library != "system.c" ||
         descriptor.symbol != "clock_gettime" || descriptor.abi != "c" ||
         descriptor.parameters != std::vector<vietvm::bytecode::ForeignAbiType>{
             vietvm::bytecode::ForeignAbiType::I32,
             vietvm::bytecode::ForeignAbiType::BufferOut} ||
         descriptor.result != vietvm::bytecode::ForeignAbiType::I32)) {
        throw ffiError(descriptor.capability +
                       " chỉ cho phép clock_gettime(i32,c_đệm_ra):i32");
    }
    if (descriptor.abi != "c") {
        throw ffiError("ABI chưa hỗ trợ: " + descriptor.abi);
    }
    if (arguments.size() != descriptor.parameters.size()) {
        throw ffiError("số đối số không khớp descriptor");
    }
    // Source descriptors cover every buffer. Legacy C++ fixture descriptors
    // without metadata still use the narrow per-capability checks below.
    if (!descriptor.bufferExtents.empty()) {
        std::vector<bool> covered(descriptor.parameters.size(), false);
        for (const auto &extent : descriptor.bufferExtents) {
            if (extent.parameterIndex >= descriptor.parameters.size() ||
                covered[extent.parameterIndex]) {
                throw ffiError("descriptor đệm FFI có chỉ số trùng hoặc ngoài phạm vi");
            }
            const auto bufferType = descriptor.parameters[extent.parameterIndex];
            if (bufferType != ForeignAbiType::BufferOut &&
                bufferType != ForeignAbiType::BufferIn) {
                throw ffiError("descriptor độ dài FFI tham chiếu tham số không phải đệm");
            }
            covered[extent.parameterIndex] = true;
            if (extent.lengthParameterIndex < 0) {
                if (extent.fixedLength == 0 || extent.fixedLength > 65536) {
                    throw ffiError("độ dài cố định của đệm FFI ngoài miền 1..65536");
                }
            } else {
                if (extent.fixedLength != 0 ||
                    static_cast<std::size_t>(extent.lengthParameterIndex) >=
                        descriptor.parameters.size()) {
                    throw ffiError("tham chiếu độ dài đệm FFI không hợp lệ");
                }
                const auto lengthType =
                    descriptor.parameters[extent.lengthParameterIndex];
                if (lengthType != ForeignAbiType::I32 &&
                    lengthType != ForeignAbiType::U32 &&
                    lengthType != ForeignAbiType::I64 &&
                    lengthType != ForeignAbiType::U64) {
                    throw ffiError("độ dài đệm FFI phải tham chiếu ABI số nguyên");
                }
            }
        }
        for (std::size_t index = 0; index < covered.size(); ++index) {
            if ((descriptor.parameters[index] == ForeignAbiType::BufferOut ||
                 descriptor.parameters[index] == ForeignAbiType::BufferIn) &&
                !covered[index]) {
                throw ffiError("descriptor FFI thiếu độ dài cho tham số đệm");
            }
        }
    }
    if (descriptor.result == ForeignAbiType::BufferOut ||
        descriptor.result == ForeignAbiType::BufferIn ||
        (descriptor.result == ForeignAbiType::FileHandle && !fileOpen) ||
        (descriptor.result == ForeignAbiType::DirectoryHandle && !dirOpen) ||
        (descriptor.result == ForeignAbiType::DirectoryEntry && !dirRead) ||
        (descriptor.result == ForeignAbiType::PointerStatus && !localClock) ||
        (descriptor.result == ForeignAbiType::DnsHandle && !dnsOpen) ||
        (descriptor.result == ForeignAbiType::SocketHandle &&
            !sockConnect && !sockListen && !sockAccept && !tlsUpgrade)) {
        throw ffiError("kiểu buffer/handle không hợp lệ ở vị trí kết quả");
    }
    // Extent metadata cannot grant permission for a new native pointer API.
    // Buffer direction comes from ABI type; ownership is per-call copied.
    for (const auto type : descriptor.parameters) {
        if (type == vietvm::bytecode::ForeignAbiType::BufferOut &&
            !monotonicClock && !realtimeClock && !entropyRead && !fileRead &&
            !statFollow && !statNoFollow && !localClock && !sockRecv && !tlsRecv &&
            !(processBinding && processOp == "read")) {
            throw ffiError("c_đệm_ra chỉ hỗ trợ binding đã kiểm tra");
        }
        if (type == ForeignAbiType::BufferIn && !fileWrite && !localClock &&
            !sockSend && !tlsSend) {
            throw ffiError("c_đệm_vào chỉ hỗ trợ binding ghi tệp đã kiểm tra");
        }
        if (type == ForeignAbiType::FileHandle &&
            !fileRead && !fileWrite && !fileClose && !fileError) {
            throw ffiError("c_tệp chỉ hỗ trợ binding stdio có quản lý vòng đời");
        }
        if (type == ForeignAbiType::DirectoryHandle && !dirRead && !dirClose) {
            throw ffiError("c_thư_mục chỉ hỗ trợ readdir/closedir do VM quản lý");
        }
        if (type == ForeignAbiType::DirectoryEntry) {
            throw ffiError("c_mục_thư_mục chỉ là kết quả readdir");
        }
        if (type == ForeignAbiType::PointerStatus) {
            throw ffiError("c_cờ_con_trỏ chỉ là kết quả kiểm tra localtime_r");
        }
        if (type == ForeignAbiType::DnsHandle && !dnsNext && !dnsClose) {
            throw ffiError("c_dns chỉ được sử dụng với resolver VM");
        }
        if (type == ForeignAbiType::SocketHandle && !sockAccept && !sockTimeout &&
            !sockKind && !sockSend && !sockRecv && !sockClose && !tlsBinding) {
            throw ffiError("c_socket chỉ được dùng với socket VM");
        }
    }

    // ABI boundary metadata. These are VM-owned queries rather than libc
    // symbols: errno must be captured before any subsequent native work.
    if (descriptor.capability == "system.ffi.error") {
        if (descriptor.library != "system.ffi" ||
            !descriptor.parameters.empty() ||
            descriptor.result != vietvm::bytecode::ForeignAbiType::I32 ||
            (descriptor.symbol != "last_errno" && descriptor.symbol != "eintr" &&
             descriptor.symbol != "eexist" && descriptor.symbol != "enoent")) {
            throw ffiError("system.ffi.error chỉ cho phép last_errno()/eintr()/eexist()/enoent():i32");
        }
        const int errorValue = descriptor.symbol == "eintr" ? EINTR
            : descriptor.symbol == "eexist" ? EEXIST
            : descriptor.symbol == "enoent" ? ENOENT : previousPosixError;
        return {make_int_value(errorValue),
                previousPosixError};
    }
    if (descriptor.capability == "system.ffi.layout") {
        if (descriptor.library != "system.ffi" ||
            descriptor.abi != "c" || !descriptor.parameters.empty() ||
            descriptor.result != vietvm::bytecode::ForeignAbiType::I32 ||
            descriptor.symbol != "stat_mode_offset") {
            throw ffiError("system.ffi.layout chỉ cho phép stat_mode_offset():i32");
        }
#if !defined(_WIN32)
        if (sizeof(struct stat) > 256 || sizeof(mode_t) < 2 ||
            offsetof(struct stat, st_mode) + 2 > sizeof(struct stat)) {
            throw ffiError("layout stat không hỗ trợ trên nền tảng này");
        }
        return {make_int_value(static_cast<int>(offsetof(struct stat, st_mode))),
                previousPosixError};
#else
        throw ffiError("layout stat chưa hỗ trợ trên Windows");
#endif
    }

#if defined(_WIN32)
    // Windows adapters keep the exact audited FFI descriptors but use Win32
    // wide APIs rather than resolving incompatible POSIX symbols from a DLL.
    const auto windowsString = [&](std::size_t index) -> const std::string & {
        if (!std::holds_alternative<std::string>(arguments.at(index)))
            throw ffiError("Windows FFI yêu cầu c_chuỗi");
        const auto &value = std::get<std::string>(arguments[index]);
        if (value.find('\0') != std::string::npos)
            throw ffiError("Windows FFI c_chuỗi chứa NUL");
        return value;
    };
    const auto windowsI32 = [&](std::size_t index) -> int {
        if (!std::holds_alternative<int>(arguments.at(index)))
            throw ffiError("Windows FFI yêu cầu i32");
        return std::get<int>(arguments[index]);
    };
    const auto windowsBuffer = [&](std::size_t index, bool input,
                                   std::size_t minSize, std::size_t maxSize)
                                   -> std::vector<StackValue> & {
        if (!std::holds_alternative<ListHandle>(arguments.at(index)) ||
            !std::get<ListHandle>(arguments[index]))
            throw ffiError("Windows FFI yêu cầu c_đệm hợp lệ");
        auto &bytes = std::get<ListHandle>(arguments[index])->elements;
        if (bytes.size() < minSize || bytes.size() > maxSize)
            throw ffiError("Windows FFI: độ dài đệm không hợp lệ");
        if (input) {
            for (const auto &element : bytes) {
                if (!std::holds_alternative<int>(element) ||
                    std::get<int>(element) < 0 || std::get<int>(element) > 255)
                    throw ffiError("Windows FFI: byte phải trong miền 0..255");
            }
        }
        // Preserve descriptor extents, including source-supplied fixed lengths.
        bool covered = false;
        for (const auto &extent : descriptor.bufferExtents) {
            if (extent.parameterIndex != index) continue;
            covered = true;
            const auto expected = extent.lengthParameterIndex < 0
                ? static_cast<std::uint64_t>(extent.fixedLength)
                : ffiUnsigned64(arguments.at(
                    static_cast<std::size_t>(extent.lengthParameterIndex)));
            if (expected != bytes.size())
                throw ffiError("Windows FFI: độ dài đệm không khớp descriptor");
        }
        if (!descriptor.bufferExtents.empty() && !covered)
            throw ffiError("Windows FFI: thiếu extent của đệm");
        return bytes;
    };
    if (windowsPathKind) {
        // -1 = missing, 0 = other/reparse, 1 = regular file, 2 = directory.
        // Follow links for public queries; never follow them during tree
        // removal. Keep Win32 handles and error conversion inside this ABI.
        const int follow = windowsI32(1);
        if (follow != 0 && follow != 1)
            throw ffiError("system.fs.path.kind: theo_liên_kết chỉ nhận 0 hoặc 1");
        const auto path = foreignWide(windowsString(0));
        if (path.empty()) return {make_int_value(-1), ENOENT};
        const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS |
            (follow == 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0);
        HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, flags, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const int error = foreignWindowsPathErrno(GetLastError());
            if (error == ENOENT || error == ENOTDIR)
                return {make_int_value(-1), error};
            throw ffiError("system.fs.path.kind: không thể đọc trạng thái, errno=" +
                           std::to_string(error));
        }
        BY_HANDLE_FILE_INFORMATION metadata{};
        const BOOL ok = GetFileInformationByHandle(handle, &metadata);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(handle);
        if (!ok)
            throw ffiError("system.fs.path.kind: không thể đọc thuộc tính, errno=" +
                           std::to_string(foreignWindowsPathErrno(error)));
        const DWORD attributes = metadata.dwFileAttributes;
        const int kind = (follow == 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            ? 0 : (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 2
            : (attributes & FILE_ATTRIBUTE_DEVICE) ? 0 : 1;
        return {make_int_value(kind), 0};
    }
    if (descriptor.capability == "system.env.read") {
        const auto &key = windowsString(0);
        if (key.empty() || key.find('=') != std::string::npos)
            throw ffiError("getenv: tên biến môi trường không hợp lệ");
        const auto wide = foreignWide(key);
        SetLastError(ERROR_SUCCESS);
        const DWORD needed = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
        if (needed == 0) {
            const DWORD code = GetLastError();
            if (code == ERROR_ENVVAR_NOT_FOUND)
                return {make_null_value(), 0};
            if (code == ERROR_SUCCESS)
                return {make_string_value(""), 0};
            throw ffiError("GetEnvironmentVariableW thất bại");
        }
        std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
        const DWORD copied = GetEnvironmentVariableW(wide.c_str(),
            buffer.data(), needed);
        if (copied >= needed || (copied == 0 && GetLastError() != ERROR_SUCCESS))
            throw ffiError("GetEnvironmentVariableW: môi trường thay đổi lúc đọc");
        buffer.resize(copied);
        return {make_string_value(foreignUtf8(buffer)), 0};
    }
    if (descriptor.capability == "system.process.id")
        return {make_int_value(static_cast<int>(GetCurrentProcessId())), 0};
    if (descriptor.capability == "system.process.parent_id") {
        const int parent = foreignParentProcessId();
        if (parent < 0) throw ffiError("không đọc được PID tiến trình cha");
        return {make_int_value(parent), 0};
    }
    if (descriptor.capability == "system.time.sleep") {
        const std::uint32_t micros = ffiUnsigned32(arguments[0]);
        // Round up to the Windows millisecond clock; never return early.
        Sleep(static_cast<DWORD>((static_cast<std::uint64_t>(micros) + 999) / 1000));
        return {make_int_value(0), 0};
    }
    if (monotonicClock || realtimeClock) {
        const int clockId = windowsI32(0);
        if (clockId != (monotonicClock ? 1 : 0))
            throw ffiError("Windows clock: clock id không đúng capability");
        auto &bytes = windowsBuffer(1, false, 16, 16);
        std::vector<unsigned char> output(16);
        if (monotonicClock) {
            LARGE_INTEGER frequency{}, ticks{};
            if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
                !QueryPerformanceCounter(&ticks) || ticks.QuadPart < 0)
                throw ffiError("QueryPerformanceCounter thất bại");
            const std::uint64_t count = static_cast<std::uint64_t>(ticks.QuadPart);
            const std::uint64_t divisor = static_cast<std::uint64_t>(frequency.QuadPart);
            foreignTimespecBytes(output, count / divisor,
                (count % divisor) * 1000000000ULL / divisor);
        } else {
            FILETIME now{};
            GetSystemTimeAsFileTime(&now);
            const std::uint64_t intervals =
                (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
            constexpr std::uint64_t epoch = 116444736000000000ULL;
            const std::int64_t unixIntervals = static_cast<std::int64_t>(intervals) -
                                               static_cast<std::int64_t>(epoch);
            std::int64_t seconds = unixIntervals / 10000000;
            std::int64_t remainder = unixIntervals % 10000000;
            if (remainder < 0) { --seconds; remainder += 10000000; }
            foreignTimespecBytes(output, static_cast<std::uint64_t>(seconds),
                                 static_cast<std::uint64_t>(remainder) * 100);
        }
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = make_int_value(output[i]);
        return {make_int_value(0), 0};
    }
    if (localClock) {
        // The audited wire format matches POSIX struct tm field offsets.
        // Windows CRT owns timezone/DST conversion; V++ derives the UTC
        // offset from these civil fields and the original epoch.
        const auto &input = windowsBuffer(0, true, 8, 8);
        auto &output = windowsBuffer(1, false, 64, 64);
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits |= static_cast<std::uint64_t>(std::get<int>(input[i])) << (i * 8);
        }
        std::int64_t signedEpoch = 0;
        static_assert(sizeof(signedEpoch) == sizeof(bits));
        std::memcpy(&signedEpoch, &bits, sizeof(bits));
        const __time64_t epoch = static_cast<__time64_t>(signedEpoch);
        struct tm calendar{};
        if (_localtime64_s(&calendar, &epoch) != 0)
            return {make_int_value(0), 0};
        for (auto &byte : output) byte = make_int_value(0);
        const std::array<int, 6> fields = {
            calendar.tm_sec, calendar.tm_min, calendar.tm_hour,
            calendar.tm_mday, calendar.tm_mon, calendar.tm_year};
        for (std::size_t field = 0; field < fields.size(); ++field) {
            const auto bits32 = static_cast<std::uint32_t>(fields[field]);
            for (int index = 0; index < 4; ++index) {
                output[field * 4 + static_cast<std::size_t>(index)] =
                    make_int_value(static_cast<int>((bits32 >> (index * 8)) & 255u));
            }
        }
        return {make_int_value(1), 0};
    }
    if (entropyRead) {
        auto &bytes = windowsBuffer(0, false, 1, 256);
        if (ffiUnsigned64(arguments[1]) != bytes.size())
            throw ffiError("getentropy: độ dài không khớp");
        std::array<unsigned char, 256> output{};
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, output.data(),
                static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            return {make_int_value(-1), EIO};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = make_int_value(output[i]);
        return {make_int_value(0), 0};
    }
    if (descriptor.capability == "system.fs.mkdir") {
        const auto &path = windowsString(0);
        if (ffiUnsigned32(arguments[1]) != 0777u)
            throw ffiError("mkdir: mode chỉ cho phép 0777");
        const std::wstring widePath = foreignWide(path);
        if (widePath.empty()) return {make_int_value(-1), ENOENT};
        if (CreateDirectoryW(widePath.c_str(), nullptr))
            return {make_int_value(0), 0};
        return {make_int_value(-1), foreignWindowsPathErrno(GetLastError())};
    }
    if (descriptor.capability == "system.fs.remove") {
        const std::wstring widePath = foreignWide(windowsString(0));
        if (widePath.empty()) return {make_int_value(-1), ENOENT};
        const DWORD attributes = GetFileAttributesW(widePath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return {make_int_value(-1), foreignWindowsPathErrno(GetLastError())};
        // RemoveDirectoryW deletes a directory reparse point itself; it does
        // not traverse the target. Tree traversal remains in V++.
        const BOOL success = (attributes & FILE_ATTRIBUTE_DIRECTORY)
            ? RemoveDirectoryW(widePath.c_str()) : DeleteFileW(widePath.c_str());
        if (success) return {make_int_value(0), 0};
        return {make_int_value(-1), foreignWindowsPathErrno(GetLastError())};
    }
    if (dirOpen || dirRead || dirClose) {
        if (dirOpen) {
            const std::wstring path = foreignWide(windowsString(0));
            if (path.empty()) return {make_null_value(), ENOENT};
            const DWORD attrs = GetFileAttributesW(path.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
                return {make_null_value(), foreignWindowsPathErrno(GetLastError())};
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return {make_null_value(), ENOTDIR};

            const auto candidate = nextForeignResourceToken.fetch_add(1);
            if (candidate > static_cast<std::uint64_t>(INT32_MAX))
                throw ffiError("hết định danh thư mục FFI");
            // Keep the search handle safe if inserting the VM token throws.
            std::unique_ptr<ForeignWindowsDirectory,
                decltype(&closeForeignWindowsDirectory)> directory(
                    new ForeignWindowsDirectory(), &closeForeignWindowsDirectory);
            std::wstring pattern = path;
            if (pattern.back() != L'/' && pattern.back() != L'\\')
                pattern += L'\\';
            pattern += L'*';
            directory->search = FindFirstFileW(pattern.c_str(), &directory->entry);
            if (directory->search == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                if (error != ERROR_FILE_NOT_FOUND)
                    return {make_null_value(), foreignWindowsPathErrno(error)};
                // An existing empty directory has no matches for "path\\*".
                directory->exhausted = true;
            } else {
                directory->firstPending = true;
            }
            const int token = static_cast<int>(candidate);
            fileState->directories.emplace(token, directory.get());
            (void)directory.release();
            return {make_int_value(token), 0};
        }
        const int token = windowsI32(0);
        const auto found = fileState->directories.find(token);
        if (found == fileState->directories.end())
            throw ffiError("c_thư_mục không thuộc VM hoặc đã đóng");
        auto *directory = static_cast<ForeignWindowsDirectory *>(found->second);
        if (dirClose) {
            // The VM token is invalid immediately, even if FindClose fails.
            fileState->directories.erase(found);
            BOOL success = TRUE;
            DWORD error = ERROR_SUCCESS;
            if (directory->search != INVALID_HANDLE_VALUE) {
                success = FindClose(directory->search);
                if (!success) error = GetLastError();
                directory->search = INVALID_HANDLE_VALUE;
            }
            closeForeignWindowsDirectory(directory);
            return {make_int_value(success ? 0 : -1),
                success ? 0 : foreignWindowsPathErrno(error)};
        }
        if (directory->exhausted) return {make_null_value(), 0};
        if (directory->firstPending) {
            directory->firstPending = false;
        } else if (!FindNextFileW(directory->search, &directory->entry)) {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_FILES) {
                directory->exhausted = true;
                return {make_null_value(), 0};
            }
            return {make_null_value(), foreignWindowsPathErrno(error)};
        }
        return {make_string_value(foreignUtf8(directory->entry.cFileName)), 0};
    }
    if (fileBinding) {
        if (fileOpen) {
            const auto &path = windowsString(0);
            const auto &mode = windowsString(1);
            if (mode != "rb" && mode != "wb" && mode != "ab")
                throw ffiError("fopen: chế độ chỉ được rb, wb hoặc ab");
            const auto widePath = foreignWide(path);
            if (widePath.empty()) throw ffiError("fopen: đường dẫn rỗng");
            const std::uint64_t candidate = nextForeignResourceToken.fetch_add(1);
            if (candidate > INT32_MAX) throw ffiError("hết định danh tệp FFI");
            errno = 0;
            std::FILE *file = _wfopen(widePath.c_str(),
                mode == "rb" ? L"rb" : mode == "wb" ? L"wb" : L"ab");
            const int error = errno;
            if (file == nullptr) return {make_null_value(), error};
            try {
                fileState->files.emplace(static_cast<int>(candidate), file);
            } catch (...) { std::fclose(file); throw; }
            return {make_int_value(static_cast<int>(candidate)), 0};
        }
        const std::size_t handleIndex = fileRead || fileWrite ? 3 : 0;
        const int token = windowsI32(handleIndex);
        const auto found = fileState->files.find(token);
        if (found == fileState->files.end())
            throw ffiError("c_tệp không thuộc VM hoặc đã đóng");
        std::FILE *file = found->second;
        if (fileError) return {make_int_value(std::ferror(file)), 0};
        if (fileClose) {
            fileState->files.erase(found);
            errno = 0;
            const int result = std::fclose(file);
            return {make_int_value(result), errno};
        }
        auto &bytes = windowsBuffer(0, fileWrite, 1, 65536);
        if (ffiUnsigned64(arguments[1]) != 1 ||
            ffiUnsigned64(arguments[2]) != bytes.size())
            throw ffiError("fread/fwrite: size=1 và count phải khớp đệm");
        std::vector<unsigned char> native(bytes.size(), 0);
        if (fileWrite) {
            for (std::size_t i = 0; i < bytes.size(); ++i)
                native[i] = static_cast<unsigned char>(std::get<int>(bytes[i]));
        }
        errno = 0;
        const std::size_t n = fileRead
            ? std::fread(native.data(), 1, native.size(), file)
            : std::fwrite(native.data(), 1, native.size(), file);
        const int error = errno;
        if (fileRead) {
            for (std::size_t i = 0; i < n; ++i)
                bytes[i] = make_int_value(native[i]);
        }
        return {make_abi_integer_value(AbiInteger::unsignedNumber(n)), error};
    }
#endif

    if (processBinding) {
#if defined(VPP_HAS_LIBFFI) || defined(_WIN32)
        auto str = [&](std::size_t index) -> const std::string & {
            if (index >= arguments.size() ||
                !std::holds_alternative<std::string>(arguments[index]))
                throw ffiError("system.process yêu cầu c_chuỗi");
            const auto &result = std::get<std::string>(arguments[index]);
            if (result.find('\0') != std::string::npos)
                throw ffiError("system.process c_chuỗi chứa NUL");
            return result;
        };
        auto integer = [&](std::size_t index) -> int {
            if (index >= arguments.size() ||
                !std::holds_alternative<int>(arguments[index]))
                throw ffiError("system.process yêu cầu i32");
            return std::get<int>(arguments[index]);
        };
        if (processOp == "new") {
            const auto &program = str(0);
            if (program.empty()) throw ffiError("system.process: chương trình rỗng");
            const auto candidate = nextForeignResourceToken.fetch_add(1);
            if (candidate > static_cast<std::uint64_t>(INT32_MAX))
                throw ffiError("hết định danh tiến trình VM");
            ForeignFileState::Process process{};
            process.program = program;
            fileState->processes.emplace(static_cast<int>(candidate), std::move(process));
            return {make_int_value(static_cast<int>(candidate)), previousPosixError};
        }
        const int id = integer(0);
        auto iterator = fileState->processes.find(id);
        if (iterator == fileState->processes.end())
            throw ffiError("tiến trình không thuộc VM hoặc đã đóng");
        auto &process = iterator->second;
        if (processOp == "close") {
            closeForeignProcess(process);
            fileState->processes.erase(iterator);
            return {make_int_value(1), previousPosixError};
        }
        if (processOp == "error")
            return {make_string_value(process.error), previousPosixError};
        if (processOp == "command_line") {
#if defined(_WIN32)
            if (process.startAttempted || !process.windowsCommandLine.empty() ||
                !process.arguments.empty())
                throw ffiError("command_line chỉ được đặt một lần, trước khi spawn");
            const auto &line = str(1);
            if (line.empty() || foreignWide(line).size() > 32766)
                throw ffiError("command_line Windows rỗng hoặc vượt 32766 ký tự UTF-16");
            process.windowsCommandLine = line;
            return {make_int_value(1), previousPosixError};
#else
            throw ffiError("command_line chỉ hỗ trợ Windows");
#endif
        }
        if (processOp == "arg" || processOp == "env") {
            if (process.startAttempted)
                throw ffiError("không thể sửa argv/env sau khi spawn");
            if (processOp == "arg") {
#if defined(_WIN32)
                throw ffiError("Windows argv phải dựng tại V++ qua command_line");
#else
                process.arguments.push_back(str(1));
#endif
            } else {
                const auto &key = str(1);
                const auto &value = str(2);
                if (key.empty() || key.find('=') != std::string::npos)
                    throw ffiError("tên biến môi trường không hợp lệ");
                process.environment[key] = value;
            }
            return {make_int_value(1), previousPosixError};
        }
        if (processOp == "start") {
            if (process.startAttempted)
                throw ffiError("tiến trình đã khởi chạy");
#if defined(_WIN32)
            if (process.windowsCommandLine.empty())
                throw ffiError("Windows process yêu cầu command_line từ V++ trước khi start");
#endif
            process.startAttempted = true;
            return {make_int_value(launchForeignProcess(process) ? 1 : 0),
                    previousPosixError};
        }
        if (process.pid <= 0)
            throw ffiError("tiến trình chưa khởi chạy hoặc đã được wait");
        if (processOp == "poll") {
            const int timeout = integer(1);
            if (timeout < 0 || timeout > 5000)
                throw ffiError("poll tiến trình yêu cầu timeout 0..5000 ms");
#if defined(_WIN32)
            return {make_int_value(pollForeignWindowsProcess(process, timeout)),
                    previousPosixError};
#else
            pollfd fds[2]{};
            int count = 0;
            if (process.stdoutFd >= 0) fds[count++] = {process.stdoutFd, POLLIN, 0};
            if (process.stderrFd >= 0) fds[count++] = {process.stderrFd, POLLIN, 0};
            if (count == 0) return {make_int_value(4), previousPosixError};
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(timeout);
            int rc = -1;
            while (true) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                rc = ::poll(fds, count, static_cast<int>(std::max<std::int64_t>(0, left)));
                if (rc < 0 && errno == EINTR) continue;
                break;
            }
            if (rc < 0) throwForeignSocketIoError("process_poll: " +
                                                  std::string(std::strerror(errno)));
            int ready = 0;
            for (int i = 0; i < count; ++i) {
                if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
                    ready |= fds[i].fd == process.stdoutFd ? 1 : 2;
            }
            return {make_int_value(ready), previousPosixError};
#endif
        }
        if (processOp == "read") {
            const int channel = integer(1);
            if (channel != 1 && channel != 2)
                throw ffiError("process_read: channel yêu cầu 1 hoặc 2");
            if (!std::holds_alternative<ListHandle>(arguments[2]) ||
                !std::get<ListHandle>(arguments[2]))
                throw ffiError("process_read: c_đệm_ra yêu cầu danh sách byte");
            auto &buffer = std::get<ListHandle>(arguments[2])->elements;
            const auto count = ffiUnsigned64(arguments[3]);
            if (count == 0 || count > 4096 || buffer.size() != count)
                throw ffiError("process_read: đệm phải khớp 1..4096 byte");
            // Both platforms expose the same c_đệm_ra and 1/2 pipe channels.
#if defined(_WIN32)
            void *&slot = channel == 1 ? process.windowsStdout : process.windowsStderr;
            if (!slot) return {make_int_value(0), previousPosixError};
            DWORD available = 0;
            if (!PeekNamedPipe(static_cast<HANDLE>(slot), nullptr, 0,
                               nullptr, &available, nullptr)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                    CloseHandle(static_cast<HANDLE>(slot));
                    slot = nullptr;
                    return {make_int_value(0), previousPosixError};
                }
                throwForeignSocketIoError("process_read: mã " + std::to_string(error));
            }
            if (available == 0) return {make_int_value(0), previousPosixError};
            std::array<unsigned char, 4096> bytes{};
            DWORD n = 0;
            if (!ReadFile(static_cast<HANDLE>(slot), bytes.data(),
                          static_cast<DWORD>(std::min<std::uint64_t>(count, available)),
                          &n, nullptr)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE) {
                    CloseHandle(static_cast<HANDLE>(slot));
                    slot = nullptr;
                    return {make_int_value(0), previousPosixError};
                }
                throwForeignSocketIoError("process_read: mã " + std::to_string(error));
            }
            for (DWORD i = 0; i < n; ++i)
                buffer[static_cast<std::size_t>(i)] = make_int_value(bytes[i]);
            return {make_int_value(static_cast<int>(n)), previousPosixError};
#else
            int &fd = channel == 1 ? process.stdoutFd : process.stderrFd;
            if (fd < 0) return {make_int_value(0), previousPosixError};
            std::array<unsigned char, 4096> bytes{};
            ssize_t n = -1;
            do { n = ::read(fd, bytes.data(), static_cast<std::size_t>(count)); }
            while (n < 0 && errno == EINTR);
            if (n < 0) throwForeignSocketIoError("process_read: " +
                                                 std::string(std::strerror(errno)));
            if (n == 0) { (void)::close(fd); fd = -1; }
            for (ssize_t i = 0; i < n; ++i)
                buffer[static_cast<std::size_t>(i)] = make_int_value(bytes[i]);
            return {make_int_value(static_cast<int>(n)), previousPosixError};
#endif
        }
        if (processOp == "wait") {
#if defined(_WIN32)
            if (process.windowsStdout || process.windowsStderr)
                throw ffiError("process_wait: cần đọc hết cả hai pipe trước khi wait");
            const HANDLE child = static_cast<HANDLE>(process.windowsProcess);
            if (!child || WaitForSingleObject(child, INFINITE) != WAIT_OBJECT_0)
                throwForeignSocketIoError("process_wait: WaitForSingleObject thất bại");
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(child, &exitCode))
                throwForeignSocketIoError("process_wait: GetExitCodeProcess thất bại");
            process.pid = -1;
            return {make_int_value(static_cast<int>(exitCode)), previousPosixError};
#else
            if (process.stdoutFd >= 0 || process.stderrFd >= 0)
                throw ffiError("process_wait: cần đọc hết cả hai pipe trước khi wait");
            int status = 0;
            pid_t rc = -1;
            do { rc = ::waitpid(static_cast<pid_t>(process.pid), &status, 0); }
            while (rc < 0 && errno == EINTR);
            if (rc < 0) throwForeignSocketIoError("process_wait: " +
                                                 std::string(std::strerror(errno)));
            process.pid = -1;
            int exitCode = -1;
            if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) exitCode = 128 + WTERMSIG(status);
            return {make_int_value(exitCode), previousPosixError};
#endif
        }
        throw ffiError("system.process: thao tác không hợp lệ");
#else
        throw ffiError("system.process FFI chỉ hỗ trợ POSIX/libffi");
#endif
    }

    if (dnsOpen || dnsNext || dnsClose) {
#if defined(VPP_HAS_LIBFFI) && !defined(_WIN32)
        if (dnsOpen) {
            if (!std::holds_alternative<std::string>(arguments[0]) ||
                !std::holds_alternative<std::string>(arguments[1]) ||
                !std::holds_alternative<int>(arguments[2])) {
                throw ffiError("resolve_open yêu cầu tên máy, cổng và kiểu socket hợp lệ");
            }
            const auto &host = std::get<std::string>(arguments[0]);
            const auto &service = std::get<std::string>(arguments[1]);
            const int datagram = std::get<int>(arguments[2]);
            if (host.empty() || host.size() > 1024 || host.find('\0') != std::string::npos ||
                service.size() > 5 || service.find('\0') != std::string::npos ||
                (datagram != 0 && datagram != 1)) {
                throw ffiError("resolve_open: tên máy/cổng/kiểu không hợp lệ");
            }
            if (!service.empty()) {
                unsigned port = 0;
                const auto parsed = std::from_chars(service.data(),
                    service.data() + service.size(), port);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != service.data() + service.size() ||
                    port == 0 || port > 65535) {
                    throw ffiError("resolve_open: cổng phải trong 1..65535");
                }
            }
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = datagram ? SOCK_DGRAM : SOCK_STREAM;
            hints.ai_protocol = service.empty() ? 0 :
                (datagram ? IPPROTO_UDP : IPPROTO_TCP);
            addrinfo *head = nullptr;
            const int status = ::getaddrinfo(host.c_str(),
                service.empty() ? nullptr : service.c_str(), &hints, &head);
            if (status != 0 || head == nullptr) {
                if (head != nullptr) ::freeaddrinfo(head);
                throw ffiError("resolve_open: không phân giải được tên máy: " +
                    std::string(::gai_strerror(status)));
            }
            const auto candidate = nextForeignResourceToken.fetch_add(1);
            if (candidate > static_cast<std::uint64_t>(INT32_MAX)) {
                ::freeaddrinfo(head);
                throw ffiError("hết định danh resolver trong VM");
            }
            try {
                fileState->resolvers.emplace(static_cast<int>(candidate),
                    ForeignFileState::Resolver{head, head});
            } catch (...) {
                ::freeaddrinfo(head);
                throw;
            }
            return {make_int_value(static_cast<int>(candidate)), previousPosixError};
        }
        if (!std::holds_alternative<int>(arguments[0])) {
            throw ffiError("c_dns yêu cầu handle do VM sở hữu");
        }
        const int token = std::get<int>(arguments[0]);
        auto entry = fileState->resolvers.find(token);
        if (entry == fileState->resolvers.end()) {
            throw ffiError("c_dns đã đóng hoặc không thuộc VM");
        }
        if (dnsClose) {
            ::freeaddrinfo(static_cast<addrinfo *>(entry->second.head));
            fileState->resolvers.erase(entry);
            return {make_int_value(1), previousPosixError};
        }
        while (entry->second.cursor != nullptr) {
            auto *current = static_cast<addrinfo *>(entry->second.cursor);
            entry->second.cursor = current->ai_next;
            char host[NI_MAXHOST]{};
            if (::getnameinfo(current->ai_addr, current->ai_addrlen,
                    host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
                return {make_string_value(host), previousPosixError};
            }
        }
        return {make_null_value(), previousPosixError};
#else
        throw ffiError("resolver FFI chỉ được hỗ trợ trên POSIX có libffi");
#endif
    }

    if (sockBinding && !sockSend && !sockRecv && !sockClose) {
#if defined(VPP_HAS_LIBFFI) && !defined(_WIN32)
        const auto number = [&](std::size_t index) -> int {
            if (!std::holds_alternative<int>(arguments.at(index))) {
                throw ffiError("socket FFI yêu cầu số nguyên i32");
            }
            return std::get<int>(arguments[index]);
        };
        if (sockConnect) {
            if (!std::holds_alternative<std::string>(arguments[0])) {
                throw ffiError("socket_connect yêu cầu địa chỉ IP chuỗi");
            }
            const auto &host = std::get<std::string>(arguments[0]);
            const int port = number(1);
            const int datagram = number(2);
            const int timeout = number(3);
            if (host.empty() || host.size() > 1024 ||
                host.find('\0') != std::string::npos ||
                port <= 0 || port > 65535 ||
                (datagram != 0 && datagram != 1) ||
                timeout <= 0 || timeout > 300000) {
                throw ffiError("socket_connect: địa chỉ/cổng/timeout không hợp lệ");
            }
            const int fd = connectForeignSocket(host, port, datagram != 0, timeout);
            const int id = registerForeignSocket(*fileState, fd, datagram != 0, false);
            return {make_int_value(id), previousPosixError};
        }
        if (sockListen) {
            const int port = number(0);
            const int backlog = number(1);
            if (port <= 0 || port > 65535 || backlog <= 0 || backlog > 1024) {
                throw ffiError("socket_listen: cổng/backlog không hợp lệ");
            }
            const int fd = listenForeignSocket(port, backlog);
            return {make_int_value(registerForeignSocket(*fileState, fd, false, true)),
                    previousPosixError};
        }
        const int id = number(0);
        const auto found = fileState->sockets.find(id);
        if (found == fileState->sockets.end()) {
            throw ffiError("c_socket đã đóng hoặc không thuộc VM");
        }
        auto &socket = found->second;
        if (sockKind) {
            return {make_int_value(socket.tlsSession != nullptr ? 2 :
                socket.datagram ? 1 : 0), previousPosixError};
        }
        if (sockTimeout) {
            const int timeout = number(1);
            if (timeout <= 0 || timeout > 300000) {
                throw ffiError("socket_timeout: timeout không hợp lệ");
            }
            if (!setForeignSocketTimeout(socket.descriptor, timeout)) {
                throwForeignSocketIoError("socket_timeout: setsockopt thất bại, errno=" +
                                          std::to_string(errno));
            }
            return {make_int_value(1), previousPosixError};
        }
        if (!socket.listener) {
            throw ffiError("socket_accept yêu cầu socket lắng nghe");
        }
        sockaddr_storage peer{};
        socklen_t peerSize = sizeof(peer);
        const int fd = ::accept(socket.descriptor,
            reinterpret_cast<sockaddr *>(&peer), &peerSize);
        if (fd < 0) {
            throwForeignSocketIoError("socket_accept thất bại, errno=" + std::to_string(errno));
        }
        suppressForeignSocketSigpipe(fd);
        if (!setForeignSocketTimeout(fd, 300000)) {
            (void)::close(fd);
            throwForeignSocketIoError("socket_accept không đặt được timeout, errno=" +
                                      std::to_string(errno));
        }
        return {make_int_value(registerForeignSocket(*fileState, fd, false, false)),
                previousPosixError};
#else
        throw ffiError("socket FFI yêu cầu POSIX/libffi");
#endif
    }

    // TLS has a narrow, VM-owned provider boundary. Only an authenticated
    // socket token reaches the provider; its session pointer never reaches V++
    // or the generic dlsym/libffi pointer marshaller.
    if (tlsBinding || sockClose) {
#if defined(VPP_HAS_LIBFFI) && !defined(_WIN32)
        if (!std::holds_alternative<int>(arguments[0])) {
            throw ffiError("TLS/socket close yêu cầu token số nguyên");
        }
        const int token = std::get<int>(arguments[0]);
        auto found = fileState->sockets.find(token);
        if (found == fileState->sockets.end()) {
            throw ffiError("c_socket đã đóng hoặc không thuộc VM");
        }
        auto &socket = found->second;
        if (sockClose && socket.tlsSession != nullptr) {
            helpers::closeForeignTlsClient(socket.tlsSession);
            fileState->sockets.erase(found);
            return {make_int_value(0), previousPosixError};
        }
        if (tlsBinding) {
            if (tlsUpgrade) {
                if (socket.tlsSession != nullptr || socket.listener || socket.datagram ||
                    socket.descriptor < 0) {
                    throw ffiError("tls_upgrade yêu cầu socket TCP chưa nâng cấp");
                }
                if (!std::holds_alternative<std::string>(arguments[1])) {
                    throw ffiError("tls_upgrade yêu cầu tên máy chuỗi");
                }
                const auto &host = std::get<std::string>(arguments[1]);
                if (host.empty() || host.size() > 253 ||
                    std::any_of(host.begin(), host.end(), [](unsigned char c) {
                        return c <= 32 || c == 127;
                    })) {
                    throw ffiError("tls_upgrade yêu cầu tên máy hợp lệ");
                }
                int type = 0;
                socklen_t typeSize = sizeof(type);
                sockaddr_storage peer{};
                socklen_t peerSize = sizeof(peer);
                if (::getsockopt(socket.descriptor, SOL_SOCKET, SO_TYPE,
                                 &type, &typeSize) != 0 || type != SOCK_STREAM ||
                    ::getpeername(socket.descriptor,
                                  reinterpret_cast<sockaddr *>(&peer), &peerSize) != 0) {
                    throw ffiError("tls_upgrade yêu cầu socket stream đã kết nối");
                }
                // The provider consumes fd on both successful and failed
                // handshakes. Invalidate the registry descriptor first.
                const int fd = socket.descriptor;
                socket.descriptor = -1;
                std::string error;
                void *session = nullptr;
                try {
                    session = helpers::createForeignTlsClient(fd, host, error);
                } catch (...) {
                    fileState->sockets.erase(token);
                    throw;
                }
                if (session == nullptr) {
                    fileState->sockets.erase(token);
                    throwForeignSocketIoError(error.empty()
                        ? "tls_upgrade: TLS handshake thất bại" : error);
                }
                socket.tlsSession = session;
                socket.descriptor = fd; // for socket_timeout setsockopt only
                return {make_int_value(token), previousPosixError};
            }
            if (socket.tlsSession == nullptr || socket.listener || socket.datagram) {
                throw ffiError("tls_send/tls_recv yêu cầu phiên TLS do VM sở hữu");
            }
            if (!std::holds_alternative<ListHandle>(arguments[1]) ||
                !std::get<ListHandle>(arguments[1])) {
                throw ffiError("TLS yêu cầu danh sách byte hợp lệ");
            }
            auto &bytes = std::get<ListHandle>(arguments[1])->elements;
            const auto count = ffiUnsigned64(arguments[2]);
            if (count == 0 || count > 65536 || bytes.size() != count) {
                throw ffiError("TLS yêu cầu đệm và độ dài khớp 1..65536 byte");
            }
            std::string payload;
            std::string error;
            int transferred = 0;
            if (tlsSend) {
                payload.reserve(bytes.size());
                for (const auto &byte : bytes) {
                    if (!std::holds_alternative<int>(byte) ||
                        std::get<int>(byte) < 0 || std::get<int>(byte) > 255) {
                        throw ffiError("tls_send chứa byte ngoài miền 0..255");
                    }
                    payload.push_back(static_cast<char>(std::get<int>(byte)));
                }
                if (!helpers::writeForeignTlsClient(socket.tlsSession, payload,
                                                    transferred, error)) {
                    throwForeignSocketIoError(error);
                }
            } else {
                if (!helpers::readForeignTlsClient(socket.tlsSession,
                        static_cast<int>(count), payload, error)) {
                    throwForeignSocketIoError(error);
                }
                transferred = static_cast<int>(payload.size());
                if (transferred < 0 || static_cast<std::size_t>(transferred) > count)
                    throw ffiError("TLS provider trả về số byte vượt đệm");
                for (int i = 0; i < transferred; ++i) {
                    bytes[static_cast<std::size_t>(i)] = make_int_value(
                        static_cast<unsigned char>(payload[static_cast<std::size_t>(i)]));
                }
            }
            if (transferred < 0 || static_cast<std::size_t>(transferred) > count)
                throw ffiError("TLS provider trả về số byte không hợp lệ");
            return {make_abi_integer_value(AbiInteger::signedNumber(transferred)),
                    previousPosixError};
        }
#endif
    }

#if !defined(VPP_HAS_LIBFFI)
    (void)descriptor;
    (void)arguments;
    (void)previousPosixError;
    throw ffiError("runtime được build không có libffi");
#elif defined(_WIN32)
    throw ffiError("backend Windows chưa được bật trong lát cắt FFI đầu tiên");
#else
    void *symbol = nullptr;
    ForeignLibraryGuard library;
    if (descriptor.library == "system.c") {
        dlerror();
        symbol = dlsym(RTLD_DEFAULT, descriptor.symbol.c_str());
    } else {
        library.handle = dlopen(descriptor.library.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (library.handle == nullptr) {
            const char *error = dlerror();
            throw ffiError(error == nullptr ? "không mở được thư viện" : error);
        }
        dlerror();
        symbol = dlsym(library.handle, descriptor.symbol.c_str());
    }
    const char *symbolError = dlerror();
    if (symbolError != nullptr || symbol == nullptr) {
        throw ffiError(symbolError == nullptr ? "không tìm thấy symbol" : symbolError);
    }

    std::vector<ffi_type *> argumentTypes;
    std::vector<ArgumentStorage> storage(arguments.size());
    std::vector<void *> argumentPointers(arguments.size());
    argumentTypes.reserve(arguments.size());
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto type = descriptor.parameters[index];
        argumentTypes.push_back(ffiType(type));
        switch (type) {
            case vietvm::bytecode::ForeignAbiType::I32:
                if (!std::holds_alternative<int>(arguments[index])) {
                    throw ffiError("i32 yêu cầu giá trị số nguyên V++");
                }
                storage[index].i32 =
                    static_cast<std::int32_t>(std::get<int>(arguments[index]));
                argumentPointers[index] = &storage[index].i32;
                break;
            case vietvm::bytecode::ForeignAbiType::U32:
                storage[index].u32 = ffiUnsigned32(arguments[index]);
                argumentPointers[index] = &storage[index].u32;
                break;
            case vietvm::bytecode::ForeignAbiType::I64:
                storage[index].i64 = ffiSigned64(arguments[index]);
                argumentPointers[index] = &storage[index].i64;
                break;
            case vietvm::bytecode::ForeignAbiType::U64:
                storage[index].u64 = ffiUnsigned64(arguments[index]);
                argumentPointers[index] = &storage[index].u64;
                break;
            case vietvm::bytecode::ForeignAbiType::F64:
                if (!isNumeric(arguments[index])) {
                    throw ffiError("f64 yêu cầu giá trị số V++");
                }
                try {
                    storage[index].f64 = toDouble(arguments[index]);
                } catch (const std::runtime_error &error) {
                    throw ffiError(error.what());
                }
                argumentPointers[index] = &storage[index].f64;
                break;
            case vietvm::bytecode::ForeignAbiType::CString:
                if (!std::holds_alternative<std::string>(arguments[index])) {
                    throw ffiError("c_chuỗi yêu cầu chuỗi V++");
                }
                if (std::get<std::string>(arguments[index]).find('\0') !=
                    std::string::npos) {
                    throw ffiError("c_chuỗi không chấp nhận byte NUL bên trong");
                }
                storage[index].pointer =
                    std::get<std::string>(arguments[index]).c_str();
                argumentPointers[index] = &storage[index].pointer;
                break;
            case vietvm::bytecode::ForeignAbiType::BufferOut: {
                if (!std::holds_alternative<ListHandle>(arguments[index]) ||
                    !std::get<ListHandle>(arguments[index])) {
                    throw ffiError("c_đệm_ra yêu cầu danh sách byte");
                }
                const auto &bytes = std::get<ListHandle>(arguments[index])->elements;
                // Native writable storage stays owned by this call.
                if (bytes.empty() || bytes.size() > 65536) {
                    throw ffiError("c_đệm_ra yêu cầu từ 1 đến 65536 byte");
                }
                storage[index].buffer.resize(bytes.size(), 0);
                storage[index].pointer = reinterpret_cast<const char *>(
                    storage[index].buffer.data());
                argumentPointers[index] = &storage[index].pointer;
                break;
            }
            case ForeignAbiType::BufferIn: {
                if (!std::holds_alternative<ListHandle>(arguments[index]) ||
                    !std::get<ListHandle>(arguments[index])) {
                    throw ffiError("c_đệm_vào yêu cầu danh sách byte");
                }
                const auto &bytes = std::get<ListHandle>(arguments[index])->elements;
                if (bytes.empty() || bytes.size() > 65536) {
                    throw ffiError("c_đệm_vào yêu cầu từ 1 đến 65536 byte");
                }
                storage[index].buffer.reserve(bytes.size());
                for (const auto &value : bytes) {
                    if (!std::holds_alternative<int>(value) ||
                        std::get<int>(value) < 0 || std::get<int>(value) > 255) {
                        throw ffiError("c_đệm_vào chứa byte ngoài miền 0..255");
                    }
                    storage[index].buffer.push_back(
                        static_cast<unsigned char>(std::get<int>(value)));
                }
                storage[index].pointer = reinterpret_cast<const char *>(
                    storage[index].buffer.data());
                argumentPointers[index] = &storage[index].pointer;
                break;
            }
            case ForeignAbiType::FileHandle: {
                if (!std::holds_alternative<int>(arguments[index]) ||
                    fileState == nullptr) {
                    throw ffiError("c_tệp yêu cầu handle do VM sở hữu");
                }
                const int token = std::get<int>(arguments[index]);
                const auto entry = fileState->files.find(token);
                if (entry == fileState->files.end()) {
                    throw ffiError("c_tệp đã đóng hoặc không thuộc VM");
                }
                storage[index].fileToken = token;
                storage[index].pointer = reinterpret_cast<const char *>(entry->second);
                argumentPointers[index] = &storage[index].pointer;
                break;
            }
            case ForeignAbiType::DirectoryHandle: {
                if (fileState == nullptr ||
                    !std::holds_alternative<int>(arguments[index])) {
                    throw ffiError("c_thư_mục yêu cầu handle do VM sở hữu");
                }
                const int token = std::get<int>(arguments[index]);
                const auto entry = fileState->directories.find(token);
                if (entry == fileState->directories.end()) {
                    throw ffiError("c_thư_mục đã đóng hoặc không thuộc VM");
                }
                storage[index].directoryToken = token;
                storage[index].pointer = static_cast<const char *>(entry->second);
                argumentPointers[index] = &storage[index].pointer;
                break;
            }
            case ForeignAbiType::DirectoryEntry:
                throw ffiError("c_mục_thư_mục không hợp lệ ở vị trí tham số");
            case ForeignAbiType::PointerStatus:
                throw ffiError("c_cờ_con_trỏ không hợp lệ ở vị trí tham số");
            case ForeignAbiType::DnsHandle:
                throw ffiError("c_dns chỉ dùng với adapter resolver");
            case ForeignAbiType::SocketHandle: {
                if (!std::holds_alternative<int>(arguments[index])) {
                    throw ffiError("c_socket yêu cầu token số nguyên");
                }
                const int id = std::get<int>(arguments[index]);
                const auto found = fileState->sockets.find(id);
                if (found == fileState->sockets.end()) {
                    throw ffiError("c_socket đã đóng hoặc không thuộc VM");
                }
                if ((sockSend || sockRecv) &&
                    (found->second.listener || found->second.tlsSession != nullptr)) {
                    throw ffiError("raw send/recv không được dùng với listener hoặc TLS");
                }
                storage[index].socketToken = id;
                storage[index].i32 = found->second.descriptor;
                argumentPointers[index] = &storage[index].i32;
                break;
            }
            case vietvm::bytecode::ForeignAbiType::Void:
                throw ffiError("void không hợp lệ ở vị trí tham số");
        }
    }

    // Compare with marshalled ABI integers: no f64 rounding or signed casts.
    for (const auto &extent : descriptor.bufferExtents) {
        std::uint64_t expected = extent.fixedLength;
        if (extent.lengthParameterIndex >= 0) {
            const std::size_t lengthIndex =
                static_cast<std::size_t>(extent.lengthParameterIndex);
            switch (descriptor.parameters[lengthIndex]) {
                case ForeignAbiType::I32:
                    if (storage[lengthIndex].i32 < 0)
                        throw ffiError("độ dài đệm FFI không được âm");
                    expected = static_cast<std::uint64_t>(storage[lengthIndex].i32);
                    break;
                case ForeignAbiType::U32:
                    expected = storage[lengthIndex].u32;
                    break;
                case ForeignAbiType::I64:
                    if (storage[lengthIndex].i64 < 0)
                        throw ffiError("độ dài đệm FFI không được âm");
                    expected = static_cast<std::uint64_t>(storage[lengthIndex].i64);
                    break;
                case ForeignAbiType::U64:
                    expected = storage[lengthIndex].u64;
                    break;
                default:
                    throw ffiError("ABI độ dài đệm FFI không được hỗ trợ");
            }
        }
        if (expected != storage[extent.parameterIndex].buffer.size()) {
            throw ffiError("độ dài đệm FFI không khớp số byte thực tế");
        }
    }

    if (monotonicClock || realtimeClock) {
        const auto &output = storage[1].buffer;
        const std::uint16_t endianProbe = 1;
        const int expectedClockId = monotonicClock ? CLOCK_MONOTONIC : CLOCK_REALTIME;
        if (storage[0].i32 != expectedClockId ||
            output.size() != 16 || sizeof(time_t) != 8 ||
            sizeof(long) != 8 || sizeof(timespec) != 16 ||
            offsetof(timespec, tv_sec) != 0 ||
            offsetof(timespec, tv_nsec) != 8 ||
            *reinterpret_cast<const unsigned char *>(&endianProbe) != 1) {
            throw ffiError("clock_gettime yêu cầu clock id được cấp quyền và timespec LP64 little-endian 16 byte");
        }
    }
    if (entropyRead &&
        (sizeof(std::size_t) != 8 || storage[0].buffer.empty() ||
         storage[0].buffer.size() > 256 ||
         storage[1].u64 != storage[0].buffer.size())) {
        throw ffiError("getentropy yêu cầu size_t 64-bit, đệm 1..256 byte và độ dài khớp chính xác");
    }
    if (descriptor.capability == "system.fs.mkdir" && storage[1].u32 != 0777u) {
        throw ffiError("mkdir chỉ chấp nhận mode 0777 được giới hạn bởi umask");
    }
    if (descriptor.capability == "system.fs.exists" && storage[1].i32 != 0) {
        throw ffiError("access chỉ chấp nhận F_OK (mode=0)");
    }
    if (statFollow || statNoFollow) {
        const std::uint16_t endianProbe = 1;
        if (sizeof(struct stat) > 256 ||
            offsetof(struct stat, st_mode) + 2 > sizeof(struct stat) ||
            sizeof(mode_t) < 2 || storage[1].buffer.size() != 256 ||
            *reinterpret_cast<const unsigned char *>(&endianProbe) != 1 ||
            S_IFDIR != 0040000 || S_IFREG != 0100000 || S_IFMT != 0170000 ||
            reinterpret_cast<std::uintptr_t>(storage[1].buffer.data()) %
                alignof(struct stat) != 0) {
            throw ffiError("stat/lstat yêu cầu stat <=256 byte, aligned và mode POSIX little-endian");
        }
    }
    if (localClock) {
        const std::uint16_t endianProbe = 1;
        if (sizeof(time_t) != 8 || sizeof(long) != 8 ||
            sizeof(struct tm) > 64 ||
            offsetof(struct tm, tm_sec) != 0 ||
            offsetof(struct tm, tm_min) != 4 ||
            offsetof(struct tm, tm_hour) != 8 ||
            offsetof(struct tm, tm_mday) != 12 ||
            offsetof(struct tm, tm_mon) != 16 ||
            offsetof(struct tm, tm_year) != 20 ||
            offsetof(struct tm, tm_isdst) != 32 ||
            offsetof(struct tm, tm_gmtoff) != 40 ||
            storage[0].buffer.size() != 8 ||
            storage[1].buffer.size() != 64 ||
            reinterpret_cast<std::uintptr_t>(storage[0].buffer.data()) % alignof(time_t) != 0 ||
            reinterpret_cast<std::uintptr_t>(storage[1].buffer.data()) % alignof(struct tm) != 0 ||
            *reinterpret_cast<const unsigned char *>(&endianProbe) != 1) {
            throw ffiError("localtime_r yêu cầu time_t LP64 8 byte, tm POSIX 64 byte và alignment hợp lệ");
        }
    }
    int openedFileToken = 0;
    int openedDirectoryToken = 0;
    if (fileOpen || dirOpen) {
        const std::uint64_t candidate = nextForeignResourceToken.fetch_add(1);
        if (candidate > static_cast<std::uint64_t>(INT32_MAX)) {
            throw ffiError("hết định danh tài nguyên ngoại trong VM");
        }
        if (dirOpen) openedDirectoryToken = static_cast<int>(candidate);
        else openedFileToken = static_cast<int>(candidate);
    }
    if (fileOpen) {
        const auto &mode = std::get<std::string>(arguments[1]);
        if (mode != "rb" && mode != "wb" && mode != "ab") {
            throw ffiError("fopen chỉ chấp nhận rb, wb hoặc ab");
        }
    }
    if (fileRead || fileWrite) {
        const auto &bytes = storage[0].buffer;
        if (sizeof(std::size_t) != 8 || storage[1].u64 != 1 ||
            storage[2].u64 != bytes.size()) {
            throw ffiError("fread/fwrite yêu cầu size_t 64-bit, size=1 và count=độ dài đệm");
        }
    }
    if (sockSend || sockRecv) {
        const auto &bytes = storage[1].buffer;
        if (sizeof(std::size_t) != 8 || bytes.empty() || bytes.size() > 65536 ||
            storage[2].u64 != bytes.size() || storage[3].i32 != 0) {
            throw ffiError("send/recv yêu cầu đệm 1..65536 byte, độ dài khớp, flags=0");
        }
        if (sockSend) {
#if defined(MSG_NOSIGNAL)
            storage[3].i32 = MSG_NOSIGNAL;
#endif
            if (fileState->sockets.at(storage[0].socketToken).datagram &&
                bytes.size() > 65507) {
                throw ffiError("datagram vượt giới hạn UDP an toàn 65507 byte");
            }
        }
    }

    ffi_cif cif;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
                     static_cast<unsigned int>(argumentTypes.size()),
                     ffiType(descriptor.result), argumentTypes.data()) != FFI_OK) {
        throw ffiError("ffi_prep_cif thất bại");
    }

    ReturnStorage result;
    // POSIX functions only define errno for failure, so clear it before the
    // call to avoid exporting an unrelated stale error after success.
    errno = 0;
    ffi_call(&cif, FFI_FN(symbol),
             descriptor.result == vietvm::bytecode::ForeignAbiType::Void
                 ? nullptr
                 : &result,
             argumentPointers.data());
    const int nativeErrno = errno;

    if (fileClose) fileState->files.erase(storage[0].fileToken);
    // closedir invalidates DIR* even if it reports an error; never reuse it.
    if (dirClose) fileState->directories.erase(storage[0].directoryToken);
    // POSIX close may consume the fd even when interrupted. Treat its token as
    // invalid after the call; retrying close risks closing an unrelated fd.
    if (sockClose) fileState->sockets.erase(storage[0].socketToken);

    // getentropy guarantees no usable entropy on failure; do not expose
    // partial data to V++ if its return status is nonzero.
    const bool outputSucceeded = localClock ? result.pointer != nullptr
        : sockRecv ? result.i64 >= 0
        : (!entropyRead && !statFollow && !statNoFollow) || result.i32 == 0;
    // Commit out bytes after the ABI call. The list stays rooted by the VM
    // argument vector, while the native pointer is valid only during ffi_call.
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (descriptor.parameters[index] !=
            vietvm::bytecode::ForeignAbiType::BufferOut || !outputSucceeded) continue;
        auto &bytes = std::get<ListHandle>(arguments[index])->elements;
        for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
            bytes[offset] = make_int_value(storage[index].buffer[offset]);
        }
    }

    StackValue output = make_null_value();
    switch (descriptor.result) {
        case vietvm::bytecode::ForeignAbiType::Void:
            break;
        case vietvm::bytecode::ForeignAbiType::I32:
            output = make_int_value(static_cast<int>(result.i32));
            break;
        case vietvm::bytecode::ForeignAbiType::U32:
            if (result.u32 >
                static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                output = make_abi_integer_value(
                    AbiInteger::unsignedNumber(result.u32));
            } else {
                output = make_int_value(static_cast<int>(result.u32));
            }
            break;
        case vietvm::bytecode::ForeignAbiType::I64:
            output = make_abi_integer_value(AbiInteger::signedNumber(result.i64));
            break;
        case vietvm::bytecode::ForeignAbiType::U64:
            output = make_abi_integer_value(AbiInteger::unsignedNumber(result.u64));
            break;
        case vietvm::bytecode::ForeignAbiType::F64:
            output = make_float_value(result.f64);
            break;
        case vietvm::bytecode::ForeignAbiType::CString:
            output = result.pointer == nullptr
                ? make_null_value()
                : make_string_value(static_cast<const char *>(result.pointer));
            break;
        case vietvm::bytecode::ForeignAbiType::BufferOut:
        case ForeignAbiType::BufferIn:
            break; // Rejected before resolving the library or invoking native code.
        case ForeignAbiType::FileHandle:
            if (result.pointer == nullptr) break;
            fileState->files.emplace(openedFileToken, static_cast<std::FILE *>(result.pointer));
            output = make_int_value(openedFileToken);
            break;
        case ForeignAbiType::DirectoryHandle:
            if (result.pointer == nullptr) break;
            fileState->directories.emplace(openedDirectoryToken, result.pointer);
            output = make_int_value(openedDirectoryToken);
            break;
        case ForeignAbiType::DirectoryEntry:
            // readdir returns storage owned by DIR* and overwritten by the next
            // call. Copy d_name before returning to V++ and never expose a pointer.
            output = result.pointer == nullptr ? make_null_value()
                : make_string_value(static_cast<struct dirent *>(result.pointer)->d_name);
            break;
        case ForeignAbiType::PointerStatus:
            output = make_int_value(result.pointer == nullptr ? 0 : 1);
            break;
        case ForeignAbiType::DnsHandle:
            break; // Only the audited VM-owned resolver adapter can produce this.
        case ForeignAbiType::SocketHandle:
            break; // Only the guarded VM-owned socket adapters return tokens.
    }
    return {std::move(output), nativeErrno};
#endif
}

} // namespace vietvm::runtime
