#include <iostream>

#include "common/vm_native_helpers.h"
#include "common/vm_native_constants.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "vpp/runtime/value.h"
#include "vpp/core/message_constants.h"

namespace vietvm::helpers {

namespace {

#if defined(_WIN32)
// Chuyển chuỗi UTF-8 sang chuỗi wide trên Windows; hàm dùng API chuyển mã để truyền đường dẫn/command Unicode cho Win32.
std::optional<std::wstring> utf8ToWide(const std::string &text) {
    if (text.empty()) return std::wstring{};
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0) return std::nullopt;

    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            text.data(), static_cast<int>(text.size()),
                            wide.data(), length) != length) {
        return std::nullopt;
    }
    return wide;
}

// Nối thêm windows lệnh đối số; hàm đưa dữ liệu mới vào cuối cấu trúc đích theo đúng thứ tự hiện có.
void appendWindowsCommandArgument(std::wstring &command, const std::wstring &argument) {
    if (!command.empty()) command.push_back(L' ');

    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        command += argument;
        return;
    }

    command.push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            command.append(backslashes * 2 + 1, L'\\');
            command.push_back(L'"');
        } else {
            command.append(backslashes, L'\\');
            command.push_back(c);
        }
        backslashes = 0;
    }
    command.append(backslashes * 2, L'\\');
    command.push_back(L'"');
}

#endif

StackValue processPrimitiveResult(bool launched,
                                  int exitCode,
                                  std::string output,
                                  std::string error) {
    // Primitive boundary chỉ trả dữ liệu thô theo vị trí. Tên trường và
    // contract public thuộc gói/hệ thống/tiến trình.vi.
    return make_list_value({
        make_int_value(launched ? 1 : 0),
        make_int_value(exitCode),
        make_string_value(std::move(output)),
        make_string_value(std::move(error)),
    });
}

#if defined(_WIN32)
struct WideCaseInsensitiveLess {
    bool operator()(const std::wstring &left, const std::wstring &right) const {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    }
};

bool buildWindowsEnvironment(const MapHandle &overrides,
                             std::vector<wchar_t> &block) {
    std::map<std::wstring, std::wstring, WideCaseInsensitiveLess> env;
    LPWCH raw = GetEnvironmentStringsW();
    if (raw == nullptr) return false;
    for (const wchar_t *entry = raw; *entry != L'\0'; entry += wcslen(entry) + 1) {
        const std::wstring item(entry);
        const std::size_t separator = item.find(L'=', item.empty() || item[0] != L'=' ? 0 : 1);
        if (separator == std::wstring::npos) continue;
        env[item.substr(0, separator)] = item.substr(separator + 1);
    }
    FreeEnvironmentStringsW(raw);

    for (const auto &[key, value] : overrides->entries) {
        if (!std::holds_alternative<std::string>(value)) return false;
        auto wideKey = utf8ToWide(key);
        auto wideValue = utf8ToWide(std::get<std::string>(value));
        if (!wideKey.has_value() || !wideValue.has_value()) return false;
        env[*wideKey] = *wideValue;
    }

    block.clear();
    for (const auto &[key, value] : env) {
        block.insert(block.end(), key.begin(), key.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return true;
}

void readWindowsPipe(HANDLE pipe, std::string &output) {
    char chunk[4096];
    DWORD read = 0;
    while (ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) && read != 0) {
        output.append(chunk, read);
    }
    CloseHandle(pipe);
}
#else
void readPosixPipe(int fd, std::string &output) {
    char chunk[4096];
    for (;;) {
        const ssize_t count = ::read(fd, chunk, sizeof(chunk));
        if (count > 0) {
            output.append(chunk, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        break;
    }
    ::close(fd);
}

std::vector<std::string> mergedPosixEnvironment(const MapHandle &overrides) {
    std::unordered_map<std::string, std::string> env;
    for (char **current = environ; current != nullptr && *current != nullptr; ++current) {
        const std::string item(*current);
        const std::size_t separator = item.find('=');
        if (separator == std::string::npos) continue;
        env[item.substr(0, separator)] = item.substr(separator + 1);
    }
    for (const auto &[key, value] : overrides->entries) {
        env[key] = std::get<std::string>(value);
    }
    std::vector<std::string> result;
    result.reserve(env.size());
    for (const auto &[key, value] : env) result.push_back(key + "=" + value);
    return result;
}
#endif

} // namespace

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

bool handleNativeProcessPrimitive(const std::vector<StackValue> &args,
                                  StackValue &result,
                                  std::string &err) {
    const std::string fn = "tien_trinh_chay_vm";
    if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
    if (!std::holds_alternative<std::string>(args[0])) {
        err = fn + ": chương trình phải là chuỗi";
        return true;
    }
    if (!std::holds_alternative<ListHandle>(args[1]) ||
        std::get<ListHandle>(args[1]) == nullptr) {
        err = fn + ": đối số phải là danh sách";
        return true;
    }
    if (!std::holds_alternative<MapHandle>(args[2]) ||
        std::get<MapHandle>(args[2]) == nullptr) {
        err = fn + ": môi trường phải là từ điển";
        return true;
    }

    const std::string program = std::get<std::string>(args[0]);
    const ListHandle arguments = std::get<ListHandle>(args[1]);
    const MapHandle environment = std::get<MapHandle>(args[2]);
    std::vector<std::string> argvStorage;
    argvStorage.reserve(arguments->elements.size() + 1);
    argvStorage.push_back(program);
    for (const StackValue &value : arguments->elements) {
        if (!std::holds_alternative<std::string>(value)) {
            err = fn + ": mỗi đối số phải là chuỗi";
            return true;
        }
        argvStorage.push_back(std::get<std::string>(value));
    }
    for (const auto &[key, value] : environment->entries) {
        (void)key;
        if (!std::holds_alternative<std::string>(value)) {
            err = fn + ": giá trị biến môi trường phải là chuỗi";
            return true;
        }
    }

#if defined(_WIN32)
    std::wstring command;
    for (const std::string &argument : argvStorage) {
        auto wide = utf8ToWide(argument);
        if (!wide.has_value()) {
            err = fn + ": đối số không phải UTF-8 hợp lệ";
            return true;
        }
        appendWindowsCommandArgument(command, *wide);
    }
    std::vector<wchar_t> environmentBlock;
    if (!buildWindowsEnvironment(environment, environmentBlock)) {
        err = fn + ": không dựng được môi trường tiến trình";
        return true;
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    HANDLE nullInput = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &attributes, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stderrRead, &stderrWrite, &attributes, 0) ||
        !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead);
        if (stderrWrite) CloseHandle(stderrWrite);
        result = processPrimitiveResult(false, -1, "", "không tạo được pipe tiến trình");
        return true;
    }
    nullInput = CreateFileW(L"NUL", GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(stdoutRead); CloseHandle(stdoutWrite);
        CloseHandle(stderrRead); CloseHandle(stderrWrite);
        result = processPrimitiveResult(false, -1, "", "không mở được stdin rỗng");
        return true;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const BOOL started = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environmentBlock.data(), nullptr, &startup, &process);
    const DWORD startError = started ? ERROR_SUCCESS : GetLastError();
    CloseHandle(nullInput);
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);
    if (!started) {
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        result = processPrimitiveResult(false, -1, "",
                                        "CreateProcessW thất bại, mã " + std::to_string(startError));
        return true;
    }
    std::string output;
    std::string errorOutput;
    std::thread outReader(readWindowsPipe, stdoutRead, std::ref(output));
    std::thread errReader(readWindowsPipe, stderrRead, std::ref(errorOutput));
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    const bool gotExit = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    outReader.join();
    errReader.join();
    result = processPrimitiveResult(true, gotExit ? static_cast<int>(exitCode) : -1,
                                    std::move(output), std::move(errorOutput));
#else
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (::pipe(stdoutPipe) != 0 || ::pipe(stderrPipe) != 0) {
        const int saved = errno;
        if (stdoutPipe[0] >= 0) ::close(stdoutPipe[0]);
        if (stdoutPipe[1] >= 0) ::close(stdoutPipe[1]);
        if (stderrPipe[0] >= 0) ::close(stderrPipe[0]);
        if (stderrPipe[1] >= 0) ::close(stderrPipe[1]);
        result = processPrimitiveResult(false, -1, "", std::strerror(saved));
        return true;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderrPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdoutPipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderrPipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdoutPipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderrPipe[1]);

    std::vector<char *> argv;
    argv.reserve(argvStorage.size() + 1);
    for (std::string &value : argvStorage) argv.push_back(value.data());
    argv.push_back(nullptr);
    std::vector<std::string> envStorage = mergedPosixEnvironment(environment);
    std::vector<char *> envp;
    envp.reserve(envStorage.size() + 1);
    for (std::string &value : envStorage) envp.push_back(value.data());
    envp.push_back(nullptr);

    pid_t pid = -1;
    const int spawnStatus = posix_spawnp(&pid, program.c_str(), &actions, nullptr,
                                         argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(stdoutPipe[1]);
    ::close(stderrPipe[1]);
    if (spawnStatus != 0) {
        ::close(stdoutPipe[0]);
        ::close(stderrPipe[0]);
        result = processPrimitiveResult(false, -1, "", std::strerror(spawnStatus));
        return true;
    }

    std::string output;
    std::string errorOutput;
    std::thread outReader(readPosixPipe, stdoutPipe[0], std::ref(output));
    std::thread errReader(readPosixPipe, stderrPipe[0], std::ref(errorOutput));
    int waitStatus = 0;
    while (::waitpid(pid, &waitStatus, 0) < 0 && errno == EINTR) {}
    outReader.join();
    errReader.join();
    int exitCode = -1;
    if (WIFEXITED(waitStatus)) exitCode = WEXITSTATUS(waitStatus);
    else if (WIFSIGNALED(waitStatus)) exitCode = 128 + WTERMSIG(waitStatus);
    result = processPrimitiveResult(true, exitCode, std::move(output), std::move(errorOutput));
#endif
    return true;
}

} // namespace vietvm::helpers
