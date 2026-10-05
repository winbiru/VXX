#include <iostream>

#include "common/vm_native_helpers.h"
#include "common/vm_native_constants.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
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
#include <sys/wait.h>
#endif

#include "vpp/runtime/value.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"

#if defined(_WIN32) && defined(_MSC_VER)
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif
#endif

namespace vietvm::helpers {

namespace {

// Mở pipe để chạy lệnh hệ thống và đọc stdout; helper chọn API phù hợp nền tảng rồi trả handle dùng cho quá trình capture.
FILE *openCommandPipe(const std::string &cmd) {
#if defined(_WIN32) && defined(_MSC_VER)
    return _popen(cmd.c_str(), "r");
#else
    return popen(cmd.c_str(), "r");
#endif
}

// Đóng pipe tiến trình đã mở và trả exit status; helper dùng API nền tảng tương ứng để tránh rò handle.
int closeCommandPipe(FILE *pipe) {
#if defined(_WIN32) && defined(_MSC_VER)
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

// Quote một đối số shell bằng dấu nháy đơn; hàm escape dấu nháy đơn bên trong để chuỗi có thể ghép an toàn vào command line POSIX.
std::string shellQuoteSingle(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

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

// Chạy windows process; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runWindowsProcess(const std::vector<std::string> &arguments,
                       std::string &output,
                       DWORD &exitCode) {
    std::wstring command;
    for (const std::string &argument : arguments) {
        auto wide = utf8ToWide(argument);
        if (!wide.has_value()) return false;
        appendWindowsCommandArgument(command, *wide);
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    HANDLE nullInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION processInfo{};
    bool started = false;

    if (!CreatePipe(&readPipe, &writePipe, &attributes, 0) ||
        !SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        if (readPipe) CloseHandle(readPipe);
        if (writePipe) CloseHandle(writePipe);
        return false;
    }

    nullInput = CreateFileW(L"NUL", GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = nullInput;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startupInfo, &processInfo) != FALSE;
    CloseHandle(nullInput);
    CloseHandle(writePipe);
    writePipe = nullptr;
    if (!started) {
        CloseHandle(readPipe);
        return false;
    }

    output.clear();
    char chunk[512];
    DWORD bytesRead = 0;
    bool readOk = true;
    while (true) {
        if (ReadFile(readPipe, chunk, sizeof(chunk), &bytesRead, nullptr)) {
            if (bytesRead == 0) break;
            output.append(chunk, bytesRead);
            continue;
        }
        if (GetLastError() != ERROR_BROKEN_PIPE) readOk = false;
        break;
    }

    CloseHandle(readPipe);
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    const bool gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return readOk && gotExitCode;
}

#endif

// Cấu hình đã được parser V++ chuẩn hóa trước khi đi qua native boundary.
struct JdbcDbConfig {
    std::string engine;
    std::string host;
    int port = 0;
    std::string database;
    std::string sqlitePath;
};

// Chạy lệnh biến bắt giữ; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runCommandCapture(const std::string &cmd, std::string &output, int &rc) {
    output.clear();
    FILE *pipe = openCommandPipe(cmd);
    if (!pipe) return false;

    char chunk[512];
    while (fgets(chunk, sizeof(chunk), pipe) != nullptr) {
        output += chunk;
    }
    rc = closeCommandPipe(pipe);
    return true;
}

// Chạy my SQL query; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runMySqlQuery(const JdbcDbConfig &cfg,
                   const std::string &user,
                   const std::string &password,
                   const std::string &sql,
                   bool useDatabase,
                   std::string &output,
    std::string &reason) {
    if (user.empty()) {
        reason = vietvm::constants::kDbReasonMissingUsername;
        return false;
    }

    std::string mysqlBin = "$(command -v mysql || echo /opt/homebrew/opt/mysql-client/bin/mysql)";
    std::string cmd = "MYSQL_PWD=" + shellQuoteSingle(password) + " " + mysqlBin +
                      " --protocol=TCP --batch --skip-column-names -h " + shellQuoteSingle(cfg.host) +
                      " -P " + std::to_string(cfg.port) + " -u " + shellQuoteSingle(user);
    if (useDatabase) {
        cmd += " -D " + shellQuoteSingle(cfg.database);
    }
    cmd += " -e " + shellQuoteSingle(sql) + " 2>&1";

    int rc = 0;
    if (!runCommandCapture(cmd, output, rc)) {
        reason = vietvm::constants::kDbReasonCannotOpenMysqlProcess;
        return false;
    }
    if (rc != 0) {
        reason = output;
        return false;
    }
    return true;
}

// Chạy PostgreSQL query; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runPostgresQuery(const JdbcDbConfig &cfg,
                      const std::string &user,
                      const std::string &password,
                      const std::string &sql,
                      bool useDatabase,
                      std::string &output,
    std::string &reason) {
    if (user.empty()) {
        reason = vietvm::constants::kDbReasonMissingUsername;
        return false;
    }

    std::string db = useDatabase ? cfg.database : "postgres";
    std::string psqlBin = "$(command -v psql || echo /opt/homebrew/bin/psql)";
    std::string cmd = "PGPASSWORD=" + shellQuoteSingle(password) + " " + psqlBin +
                      " -h " + shellQuoteSingle(cfg.host) +
                      " -p " + std::to_string(cfg.port) +
                      " -U " + shellQuoteSingle(user) +
                      " -d " + shellQuoteSingle(db) +
                      " -At -c " + shellQuoteSingle(sql) + " 2>&1";

    int rc = 0;
    if (!runCommandCapture(cmd, output, rc)) {
        reason = vietvm::constants::kDbReasonCannotOpenPsqlProcess;
        return false;
    }
    if (rc != 0) {
        reason = output;
        return false;
    }
    return true;
}

// Chạy SQLite query; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool runSqliteQuery(const JdbcDbConfig &cfg,
                    const std::string &sql,
                    std::string &output,
                    std::string &reason) {
#if defined(_WIN32)
    // _popen runs through cmd.exe on Windows, where neither POSIX command
    // substitution nor single-quote escaping works. Invoke SQLite directly so
    // the database path and SQL remain individual UTF-8 arguments.
    DWORD exitCode = 0;
    if (!runWindowsProcess({"sqlite3.exe", cfg.sqlitePath, sql}, output, exitCode)) {
        reason = vietvm::constants::kDbReasonCannotOpenSqliteProcess;
        return false;
    }
    if (exitCode != 0) {
        reason = output;
        return false;
    }
#else
    std::string sqliteBin = "$(command -v sqlite3 || echo sqlite3)";
    std::string cmd = sqliteBin + " " + shellQuoteSingle(cfg.sqlitePath) +
                      " " + shellQuoteSingle(sql) + " 2>&1";

    int rc = 0;
    if (!runCommandCapture(cmd, output, rc)) {
        reason = vietvm::constants::kDbReasonCannotOpenSqliteProcess;
        return false;
    }
    if (rc != 0) {
        reason = output;
        return false;
    }
#endif
    return true;
}

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

// Kiểm tra điều kiện của `startsWith`.
bool startsWith(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

// Tạo bản sao chuỗi đã bỏ whitespace ở hai đầu; hàm không sửa dữ liệu gốc nên phù hợp cho parser helper/native argument.
std::string trimCopy(const std::string &s) {
    return vietvm::core::trim(s);
}

// Chuyển một `StackValue` đối số thành chuỗi thô mà native helper cần; hàm giữ nội dung chuỗi nguyên bản và định dạng scalar theo quy tắc runtime.
std::string argToRawString(const StackValue &v) {
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return sv_to_string(v);
}

// Giải mã đơn giản escapes; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
std::string decodeSimpleEscapes(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else if (n == '\\') out.push_back('\\');
            else out.push_back(n);
            ++i;
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
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

// Primitive DB: V++ đã parse JDBC, validate policy và dựng contract kết quả.
// Native chỉ gọi client hệ điều hành rồi trả trạng thái thô.
bool runDbExec(const std::string &engine,
               const std::string &host,
               int port,
               const std::string &target,
               const std::string &user,
               const std::string &password,
               const std::string &sql,
               bool useDatabase,
               StackValue &result,
               std::string &err) {
    (void)err;
    JdbcDbConfig cfg{};
    cfg.engine = engine;
    cfg.host = host;
    cfg.port = port;
    if (engine == "sqlite") {
        cfg.sqlitePath = target;
    } else {
        cfg.database = target;
    }

    std::string reason;
    std::string output;
    bool ok = false;
    if (cfg.engine == "mysql") {
        ok = runMySqlQuery(cfg, user, password, sql, useDatabase, output, reason);
    } else if (cfg.engine == "postgresql") {
        ok = runPostgresQuery(cfg, user, password, sql, useDatabase, output, reason);
    } else if (cfg.engine == "sqlite") {
        ok = runSqliteQuery(cfg, sql, output, reason);
    } else {
        reason = vietvm::constants::kDbReasonUnsupportedDriver;
    }

    MapValue response;
    response.entries["ok"] = make_int_value(ok ? 1 : 0);
    response.entries["output"] = make_string_value(output);
    response.entries["reason"] = make_string_value(reason);
    result = make_map_value(std::move(response));
    return true;
}

} // namespace vietvm::helpers
