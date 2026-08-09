#include <iostream>

#include "common/vm_native_helpers.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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
#endif

#include "vpp/runtime/value.h"

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

FILE *openCommandPipe(const std::string &cmd) {
#if defined(_WIN32) && defined(_MSC_VER)
    return _popen(cmd.c_str(), "r");
#else
    return popen(cmd.c_str(), "r");
#endif
}

int closeCommandPipe(FILE *pipe) {
#if defined(_WIN32) && defined(_MSC_VER)
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

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

bool runCurlHttpRequestWindows(const std::string &method,
                               const std::string &fnName,
                               const std::string &url,
                               const std::optional<std::string> &payload,
                               StackValue &result,
                               std::string &err) {
    std::vector<std::string> arguments = {
        "curl.exe", "-Ls", "--max-time", "20", "-X", method
    };
    if (payload.has_value()) {
        arguments.push_back("-H");
        arguments.push_back("Content-Type: application/json");
        arguments.push_back("--data-raw");
        arguments.push_back(*payload);
    }
    arguments.push_back(url);

    std::string data;
    DWORD exitCode = 0;
    if (!runWindowsProcess(arguments, data, exitCode)) {
        err = fnName + ": không mở được tiến trình curl";
        return true;
    }
    if (exitCode != 0) {
        err = fnName + ": curl trả về lỗi";
        return true;
    }

    result = make_string_value(data);
    return true;
}
#endif

std::string sanitizeDbToken(const std::string &s) {
    std::string out = trimCopy(s);
    if (out.empty()) return "db-command-failed";
    for (char &c : out) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == '|') c = '/';
    }
    return trimCopy(out);
}

bool isSafeDbIdentifier(const std::string &name) {
    if (name.empty()) return false;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

struct JdbcDbConfig {
    std::string engine;
    std::string host;
    int port = 0;
    std::string database;
    std::string sqlitePath;
    bool createIfNotExist = false;
};

bool parseJdbcTcpUrl(const std::string &jdbcUrl,
                     const std::string &prefix,
                     int defaultPort,
                     JdbcDbConfig &cfg,
                     std::string &reason) {
    if (!startsWith(jdbcUrl, prefix)) {
        reason = "invalid-jdbc-prefix";
        return false;
    }

    std::string rest = jdbcUrl.substr(prefix.size());
    size_t slash = rest.find('/');
    if (slash == std::string::npos || slash == 0) {
        reason = "invalid-host-or-database";
        return false;
    }

    std::string hostPort = trimCopy(rest.substr(0, slash));
    std::string dbAndQuery = rest.substr(slash + 1);

    cfg.host.clear();
    cfg.port = defaultPort;

    size_t colon = hostPort.find(':');
    if (colon == std::string::npos) {
        cfg.host = hostPort;
    } else {
        cfg.host = trimCopy(hostPort.substr(0, colon));
        std::string portText = trimCopy(hostPort.substr(colon + 1));
        if (portText.empty()) {
            reason = "invalid-port";
            return false;
        }
        try {
            cfg.port = std::stoi(portText);
        } catch (...) {
            reason = "invalid-port";
            return false;
        }
    }

    if (cfg.host.empty()) {
        reason = "missing-host";
        return false;
    }

    cfg.createIfNotExist = false;
    size_t q = dbAndQuery.find('?');
    if (q == std::string::npos) {
        cfg.database = trimCopy(dbAndQuery);
    } else {
        cfg.database = trimCopy(dbAndQuery.substr(0, q));
        std::string query = dbAndQuery.substr(q + 1);
        if (query.find("createDatabaseIfNotExist=true") != std::string::npos) {
            cfg.createIfNotExist = true;
        }
    }

    if (cfg.database.empty()) {
        reason = "missing-database";
        return false;
    }

    return true;
}

bool parseJdbcDbConfig(const std::string &driverClass,
                       const std::string &jdbcUrl,
                       JdbcDbConfig &cfg,
                       std::string &reason) {
    cfg = JdbcDbConfig{};

    if (startsWith(jdbcUrl, "jdbc:mysql://") || driverClass.find("mysql") != std::string::npos) {
        cfg.engine = "mysql";
        if (!parseJdbcTcpUrl(jdbcUrl, "jdbc:mysql://", 3306, cfg, reason)) return false;
        return true;
    }

    if (startsWith(jdbcUrl, "jdbc:postgresql://") || driverClass.find("postgresql") != std::string::npos) {
        cfg.engine = "postgresql";
        if (!parseJdbcTcpUrl(jdbcUrl, "jdbc:postgresql://", 5432, cfg, reason)) return false;
        return true;
    }

    if (startsWith(jdbcUrl, "jdbc:sqlite:") || driverClass.find("sqlite") != std::string::npos) {
        cfg.engine = "sqlite";
        cfg.sqlitePath = trimCopy(jdbcUrl.substr(std::string("jdbc:sqlite:").size()));
        if (cfg.sqlitePath.empty()) {
            reason = "missing-sqlite-path";
            return false;
        }
        return true;
    }

    reason = "unsupported-driver";
    return false;
}

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

bool runMySqlQuery(const JdbcDbConfig &cfg,
                   const std::string &user,
                   const std::string &password,
                   const std::string &sql,
                   bool useDatabase,
                   std::string &output,
                   std::string &reason) {
    if (user.empty()) {
        reason = "missing-username";
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
        reason = "cannot-open-mysql-process";
        return false;
    }
    if (rc != 0) {
        reason = sanitizeDbToken(output);
        return false;
    }

    output = trimCopy(output);
    return true;
}

bool runPostgresQuery(const JdbcDbConfig &cfg,
                      const std::string &user,
                      const std::string &password,
                      const std::string &sql,
                      bool useDatabase,
                      std::string &output,
                      std::string &reason) {
    if (user.empty()) {
        reason = "missing-username";
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
        reason = "cannot-open-psql-process";
        return false;
    }
    if (rc != 0) {
        reason = sanitizeDbToken(output);
        return false;
    }

    output = trimCopy(output);
    return true;
}

bool runSqliteQuery(const JdbcDbConfig &cfg,
                    const std::string &sql,
                    std::string &output,
                    std::string &reason) {
    std::string sqliteBin = "$(command -v sqlite3 || echo sqlite3)";
    std::string cmd = sqliteBin + " " + shellQuoteSingle(cfg.sqlitePath) +
                      " " + shellQuoteSingle(sql) + " 2>&1";

    int rc = 0;
    if (!runCommandCapture(cmd, output, rc)) {
        reason = "cannot-open-sqlite-process";
        return false;
    }
    if (rc != 0) {
        reason = sanitizeDbToken(output);
        return false;
    }

    output = trimCopy(output);
    return true;
}

bool ensureDatabaseIfRequested(const JdbcDbConfig &cfg,
                               const std::string &user,
                               const std::string &password,
                               std::string &reason) {
    if (!cfg.createIfNotExist) return true;
    if (!isSafeDbIdentifier(cfg.database)) {
        reason = "unsafe-database-name";
        return false;
    }

    std::string output;
    if (cfg.engine == "mysql") {
        std::string sql = "CREATE DATABASE IF NOT EXISTS `" + cfg.database + "`;";
        return runMySqlQuery(cfg, user, password, sql, false, output, reason);
    }

    if (cfg.engine == "postgresql") {
        std::string checkSql = "SELECT 1 FROM pg_database WHERE datname='" + cfg.database + "';";
        if (!runPostgresQuery(cfg, user, password, checkSql, false, output, reason)) return false;
        if (trimCopy(output) == "1") return true;
        std::string createSql = "CREATE DATABASE \"" + cfg.database + "\";";
        return runPostgresQuery(cfg, user, password, createSql, false, output, reason);
    }

    return true;
}

} // namespace

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

bool startsWith(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string trimCopy(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string argToRawString(const StackValue &v) {
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return sv_to_string(v);
}

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

std::string readPropertyByKey(const std::string &filePath,
                              const std::string &key,
                              const std::string &fallback) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        return fallback;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        std::string t = trimCopy(line);
        if (t.empty() || t[0] == '#') continue;

        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;

        std::string k = trimCopy(t.substr(0, eq));
        if (k != key) continue;

        return trimCopy(t.substr(eq + 1));
    }

    return fallback;
}

bool parseIntArgFromStack(const StackValue &arg,
                          const std::string &fn,
                          const std::string &label,
                          int &out,
                          std::string &err) {
    if (std::holds_alternative<int>(arg)) {
        out = std::get<int>(arg);
        return true;
    }
    try {
        out = std::stoi(argToRawString(arg));
        return true;
    } catch (...) {
        err = fn + ": " + label + " không hợp lệ";
        return false;
    }
}

bool runDbConnect(const std::string &driverClass,
                  const std::string &jdbcUrl,
                  const std::string &user,
                  const std::string &password,
                  StackValue &result,
                  std::string &err) {
    (void)err;
    JdbcDbConfig cfg;
    std::string reason;
    if (!parseJdbcDbConfig(driverClass, jdbcUrl, cfg, reason)) {
        result = make_string_value("DB_ERR|" + reason);
        return true;
    }

    if (!ensureDatabaseIfRequested(cfg, user, password, reason)) {
        result = make_string_value("DB_ERR|" + reason);
        return true;
    }

    std::string output;
    bool ok = false;
    if (cfg.engine == "mysql") {
        ok = runMySqlQuery(cfg, user, password, "SELECT 1;", true, output, reason);
    } else if (cfg.engine == "postgresql") {
        ok = runPostgresQuery(cfg, user, password, "SELECT 1;", true, output, reason);
    } else if (cfg.engine == "sqlite") {
        ok = runSqliteQuery(cfg, "SELECT 1;", output, reason);
    }

    if (!ok) {
        result = make_string_value("DB_ERR|" + reason);
        return true;
    }

    result = make_string_value("DB_OK|connected");
    return true;
}

bool runDbQuery(const std::string &driverClass,
                const std::string &jdbcUrl,
                const std::string &user,
                const std::string &password,
                const std::string &sql,
                StackValue &result,
                std::string &err) {
    (void)err;
    JdbcDbConfig cfg;
    std::string reason;
    if (!parseJdbcDbConfig(driverClass, jdbcUrl, cfg, reason)) {
        result = make_string_value("DB_ERR|" + reason);
        return true;
    }

    std::string output;
    bool ok = false;
    if (cfg.engine == "mysql") {
        ok = runMySqlQuery(cfg, user, password, sql, true, output, reason);
    } else if (cfg.engine == "postgresql") {
        ok = runPostgresQuery(cfg, user, password, sql, true, output, reason);
    } else if (cfg.engine == "sqlite") {
        ok = runSqliteQuery(cfg, sql, output, reason);
    }

    if (!ok) {
        result = make_string_value("DB_ERR|" + reason);
        return true;
    }

    if (output.empty()) {
        result = make_string_value("DB_OK|affected=1");
        return true;
    }

    result = make_string_value("DB_OK|" + sanitizeDbToken(output));
    return true;
}

bool runCurlHttpRequest(const std::string &method,
                        const std::string &fnName,
                        const std::string &url,
                        const std::optional<std::string> &payload,
                        StackValue &result,
                        std::string &err) {
#if defined(_WIN32)
    // _popen routes through cmd.exe, whose quoting rules are incompatible with
    // JSON and with the POSIX single-quote command used below. Execute curl
    // directly so each URL/header/payload remains one argument on Windows.
    return runCurlHttpRequestWindows(method, fnName, url, payload, result, err);
#else
    std::string cmd = "curl -Ls --max-time 20 -X " + method;
    if (payload.has_value()) {
        cmd += " -H 'Content-Type: application/json' --data " + shellQuoteSingle(*payload);
    }
    cmd += " " + shellQuoteSingle(url);

    FILE *pipe = openCommandPipe(cmd);
    if (!pipe) {
        err = fnName + ": không mở được tiến trình curl";
        return true;
    }

    std::string data;
    char chunk[512];
    while (fgets(chunk, sizeof(chunk), pipe) != nullptr) {
        data += chunk;
    }

    int rc = closeCommandPipe(pipe);
    if (rc != 0) {
        err = fnName + ": curl trả về lỗi";
        return true;
    }

    result = make_string_value(data);
    return true;
#endif
}

} // namespace vietvm::helpers
