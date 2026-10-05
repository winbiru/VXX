#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <climits>
#include <unistd.h>
#endif

#ifndef VDK_VERSION_STRING
#define VDK_VERSION_STRING "1.0.0"
#endif

namespace fs = std::filesystem;

namespace {

struct ReleaseInfo {
    std::string version;
    std::string platform;
    std::string arch;
    std::string vppRelativePath;
    std::string libraryCatalog;
    std::string packageRoot;
};

struct ProjectPin {
    fs::path file;
    std::string version;
};

std::string readTextFile(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("không mở được tệp: " + path.u8string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::optional<std::string> jsonString(const std::string &json,
                                      const std::string &key) {
    const std::regex pattern("\\\"" + key +
                             "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) return std::nullopt;
    return match[1].str();
}

std::vector<std::string> jsonStringValues(const std::string &json,
                                          const std::string &key) {
    const std::regex pattern("\\\"" + key +
                             "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::vector<std::string> values;
    for (std::sregex_iterator it(json.begin(), json.end(), pattern), end;
         it != end; ++it) {
        values.push_back((*it)[1].str());
    }
    return values;
}

fs::path normalizedAbsolute(const fs::path &path) {
    try {
        return fs::weakly_canonical(fs::absolute(path));
    } catch (...) {
        try {
            return fs::absolute(path).lexically_normal();
        } catch (...) {
            return path.lexically_normal();
        }
    }
}

ReleaseInfo readRelease(const fs::path &home) {
    const std::string json = readTextFile(home / "vdk-release.json");
    ReleaseInfo release;
    release.version = jsonString(json, "version").value_or("");
    release.platform = jsonString(json, "platform").value_or("");
    release.arch = jsonString(json, "arch").value_or("");
    release.vppRelativePath = jsonString(json, "vpp").value_or("");
    release.libraryCatalog =
        jsonString(json, "libraryCatalog").value_or("vpp-libraries.json");
    release.packageRoot = jsonString(json, "packageRoot").value_or(u8"gói");
    if (release.version.empty()) {
        throw std::runtime_error("vdk-release.json thiếu trường version");
    }
    return release;
}

std::optional<ProjectPin> findProjectPin(fs::path start) {
    start = normalizedAbsolute(start);
    for (fs::path dir = start;; dir = dir.parent_path()) {
        const fs::path config = dir / "vdk.json";
        if (fs::exists(config)) {
            const std::string json = readTextFile(config);
            const auto version = jsonString(json, "version");
            if (!version.has_value() || version->empty()) {
                throw std::runtime_error("vdk.json thiếu trường version: " +
                                         config.u8string());
            }
            return ProjectPin{config, *version};
        }
        if (dir == dir.parent_path()) break;
    }
    return std::nullopt;
}

std::optional<fs::path> environmentPath(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return normalizedAbsolute(fs::u8path(value));
}

fs::path userVdkStore() {
#if defined(_WIN32)
    if (const char *local = std::getenv("LOCALAPPDATA")) {
        if (*local != '\0') return fs::u8path(local) / "VPP" / "vdk";
    }
#endif
    if (const char *home = std::getenv("HOME")) {
        if (*home != '\0') return fs::u8path(home) / ".vpp" / "vdk";
    }
    return {};
}

fs::path currentExecutable(const std::string &argv0) {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        return normalizedAbsolute(fs::path(buffer.data()));
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return normalizedAbsolute(fs::u8path(buffer.data()));
    }
#elif defined(__linux__)
    std::vector<char> buffer(PATH_MAX + 1, '\0');
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), PATH_MAX);
    if (length > 0) {
        buffer[static_cast<std::size_t>(length)] = '\0';
        return normalizedAbsolute(fs::u8path(buffer.data()));
    }
#endif
    return normalizedAbsolute(fs::u8path(argv0));
}

bool releaseMatches(const fs::path &home, const std::string &version) {
    try {
        return readRelease(home).version == version;
    } catch (...) {
        return false;
    }
}

fs::path chooseVdkHome(const std::optional<fs::path> &explicitHome,
                       const fs::path &selfHome,
                       const std::optional<ProjectPin> &projectPin) {
    if (explicitHome.has_value()) {
        const fs::path selected = normalizedAbsolute(*explicitHome);
        if (projectPin.has_value() &&
            !releaseMatches(selected, projectPin->version)) {
            throw std::runtime_error(
                "VDK được chọn không khớp phiên bản " + projectPin->version +
                " trong " + projectPin->file.u8string());
        }
        return selected;
    }

    if (projectPin.has_value()) {
        const fs::path store = userVdkStore();
        if (!store.empty()) {
            const fs::path installed = store / fs::u8path(projectPin->version);
            if (releaseMatches(installed, projectPin->version)) return installed;
        }
        if (const auto env = environmentPath("VDK_HOME");
            env.has_value() && releaseMatches(*env, projectPin->version)) {
            return *env;
        }
        if (const auto env = environmentPath("VPP_HOME");
            env.has_value() && releaseMatches(*env, projectPin->version)) {
            return *env;
        }
        if (releaseMatches(selfHome, projectPin->version)) return selfHome;
        throw std::runtime_error(
            "không tìm thấy VDK " + projectPin->version +
            " mà dự án yêu cầu; đặt VDK_HOME hoặc dùng --vdk-home");
    }

    if (const auto env = environmentPath("VDK_HOME")) return *env;
    if (const auto env = environmentPath("VPP_HOME")) return *env;
    return selfHome;
}

fs::path vppExecutable(const fs::path &home, const ReleaseInfo &release) {
    if (!release.vppRelativePath.empty()) {
        const fs::path fromMetadata = home / fs::u8path(release.vppRelativePath);
        if (fs::exists(fromMetadata)) return fromMetadata;
    }
#if defined(_WIN32)
    const fs::path stable = home / "bin" / "vpp.exe";
    if (fs::exists(stable)) return stable;
    return home / "bin" / "vpp-cli.exe";
#else
    const fs::path stable = home / "bin" / "vpp";
    if (fs::exists(stable)) return stable;
    return home / "bin" / "vpp-cli";
#endif
}

void printUsage() {
    std::cout
        << "VDK " << VDK_VERSION_STRING << " — Bộ công cụ phát triển V++\n"
        << "Cách dùng:\n"
        << "  vdk phiên bản\n"
        << "  vdk chẩn đoán\n"
        << "  vdk thư viện danh sách [--json]\n"
        << "  vdk [--vdk-home <đường-dẫn>] chạy <tệp.vi>\n"
        << "  vdk [--vdk-home <đường-dẫn>] dựng <tệp.vi>\n"
        << "  vdk [--vdk-home <đường-dẫn>] kiểm thử [tests]\n"
        << "  vdk [--vdk-home <đường-dẫn>] gói ...\n"
        << "  vdk [--vdk-home <đường-dẫn>] --lsp | --repl\n";
}

bool wordsEqual(const std::vector<std::string> &args,
                std::size_t offset,
                std::initializer_list<const char *> words) {
    if (offset + words.size() > args.size()) return false;
    std::size_t index = offset;
    for (const char *word : words) {
        if (args[index++] != word) return false;
    }
    return true;
}

int printVersion(const fs::path &home, const ReleaseInfo &release) {
    std::cout << "VDK " << release.version;
    if (!release.platform.empty() || !release.arch.empty()) {
        std::cout << " (" << release.platform;
        if (!release.platform.empty() && !release.arch.empty()) std::cout << '-';
        std::cout << release.arch << ')';
    }
    std::cout << '\n' << "Thư mục: " << home.u8string() << '\n';
    return EXIT_SUCCESS;
}

int diagnose(const fs::path &home, const ReleaseInfo &release) {
    bool ok = true;
    auto check = [&](bool condition, const std::string &message) {
        std::cout << (condition ? "[OK] " : "[LỖI] ") << message << '\n';
        if (!condition) ok = false;
    };

    check(fs::exists(home / "vdk-release.json"), "vdk-release.json");
    const fs::path catalog = home / fs::u8path(release.libraryCatalog);
    check(fs::exists(catalog), "danh mục thư viện " + catalog.u8string());
    const fs::path packages = home / fs::u8path(release.packageRoot);
    check(fs::is_directory(packages), "thư mục thư viện " + packages.u8string());
    const fs::path vpp = vppExecutable(home, release);
    check(fs::exists(vpp), "trình biên dịch/VM " + vpp.u8string());
    check(release.version == VDK_VERSION_STRING,
          "phiên bản launcher khớp bản phân phối (" + release.version + ")");

    if (fs::exists(catalog)) {
        try {
            const std::string catalogJson = readTextFile(catalog);
            const std::string sdkVersion =
                jsonString(catalogJson, "sdkVersion").value_or("");
            check(sdkVersion == release.version,
                  "sdkVersion khớp VDK (" + sdkVersion + ")");
        } catch (const std::exception &error) {
            check(false, error.what());
        }
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int listLibraries(const fs::path &home,
                  const ReleaseInfo &release,
                  bool asJson) {
    const fs::path catalogPath = home / fs::u8path(release.libraryCatalog);
    const std::string catalog = readTextFile(catalogPath);
    if (asJson) {
        std::cout << catalog;
        if (catalog.empty() || catalog.back() != '\n') std::cout << '\n';
        return EXIT_SUCCESS;
    }

    const std::size_t groupsBegin = catalog.find("\"moduleGroups\"");
    const std::size_t librariesBegin = catalog.find("\n  \"libraries\": [");
    if (groupsBegin != std::string::npos && librariesBegin != std::string::npos &&
        groupsBegin < librariesBegin) {
        const std::string groups =
            catalog.substr(groupsBegin, librariesBegin - groupsBegin);
        const auto names = jsonStringValues(groups, "name");
        if (!names.empty()) {
            std::cout << "Mô-đun:\n";
            for (const auto &name : names) std::cout << "  " << name << '\n';
        }
    }

    const std::string libraries = librariesBegin == std::string::npos
                                      ? catalog
                                      : catalog.substr(librariesBegin);
    const auto ids = jsonStringValues(libraries, "id");
    std::cout << "Thư viện (" << ids.size() << "):\n";
    for (const auto &id : ids) std::cout << "  " << id << '\n';
    return EXIT_SUCCESS;
}

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string &text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                              text.data(),
                                              static_cast<int>(text.size()),
                                              nullptr, 0);
    if (required <= 0) throw std::runtime_error("không chuyển được UTF-8 sang UTF-16");
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            text.data(), static_cast<int>(text.size()),
                            wide.data(), required) != required) {
        throw std::runtime_error("không chuyển được UTF-8 sang UTF-16");
    }
    return wide;
}

std::string wideToUtf8(const wchar_t *text) {
    if (text == nullptr || *text == L'\0') return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                              text, -1, nullptr, 0,
                                              nullptr, nullptr);
    if (required <= 0) throw std::runtime_error("không chuyển được UTF-16 sang UTF-8");
    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            text, -1, utf8.data(), required,
                            nullptr, nullptr) != required) {
        throw std::runtime_error("không chuyển được UTF-16 sang UTF-8");
    }
    utf8.pop_back();
    return utf8;
}
#endif

int forwardToVpp(const fs::path &home,
                 const ReleaseInfo &release,
                 const std::vector<std::string> &args) {
    const fs::path vpp = vppExecutable(home, release);
    if (!fs::exists(vpp)) {
        throw std::runtime_error("không tìm thấy bin/vpp trong VDK: " +
                                 home.u8string());
    }

#if defined(_WIN32)
    const std::wstring homeWide = home.wstring();
    _wputenv_s(L"VDK_HOME", homeWide.c_str());
    _wputenv_s(L"VPP_HOME", homeWide.c_str());

    std::vector<std::wstring> wideArgs;
    wideArgs.reserve(args.size());
    wideArgs.push_back(vpp.wstring());
    for (std::size_t index = 1; index < args.size(); ++index) {
        wideArgs.push_back(utf8ToWide(args[index]));
    }
    std::vector<const wchar_t *> argv;
    argv.reserve(wideArgs.size() + 1);
    for (const auto &argument : wideArgs) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const intptr_t code = _wspawnv(_P_WAIT, vpp.wstring().c_str(), argv.data());
    if (code == -1) throw std::runtime_error("không khởi chạy được bin/vpp");
    return static_cast<int>(code);
#else
    const std::string homeUtf8 = home.u8string();
    setenv("VDK_HOME", homeUtf8.c_str(), 1);
    setenv("VPP_HOME", homeUtf8.c_str(), 1);

    std::vector<std::string> forwarded;
    forwarded.reserve(args.size());
    forwarded.push_back(vpp.u8string());
    for (std::size_t index = 1; index < args.size(); ++index) {
        forwarded.push_back(args[index]);
    }
    std::vector<char *> argv;
    argv.reserve(forwarded.size() + 1);
    for (auto &argument : forwarded) argv.push_back(argument.data());
    argv.push_back(nullptr);
    execv(vpp.c_str(), argv.data());
    throw std::runtime_error("không khởi chạy được bin/vpp");
#endif
}

int runVdk(std::vector<std::string> args) {
    if (args.empty()) return EXIT_FAILURE;

    std::optional<fs::path> explicitHome;
    std::vector<std::string> cleaned;
    cleaned.push_back(args.front());
    for (std::size_t index = 1; index < args.size(); ++index) {
        if (args[index] == "--vdk-home") {
            if (index + 1 >= args.size()) {
                throw std::runtime_error("--vdk-home cần một đường dẫn");
            }
            explicitHome = fs::u8path(args[++index]);
            continue;
        }
        cleaned.push_back(args[index]);
    }
    args = std::move(cleaned);

    if (args.size() == 1 || args[1] == "--help" || args[1] == "-h" ||
        args[1] == "help") {
        printUsage();
        return EXIT_SUCCESS;
    }

    const fs::path selfHome = currentExecutable(args[0]).parent_path().parent_path();
    const std::optional<ProjectPin> projectPin = findProjectPin(fs::current_path());
    const fs::path home = chooseVdkHome(explicitHome, selfHome, projectPin);
    const ReleaseInfo release = readRelease(home);

    if (projectPin.has_value() && release.version != projectPin->version) {
        throw std::runtime_error("VDK " + release.version +
                                 " không khớp dự án yêu cầu " +
                                 projectPin->version);
    }

    if (args[1] == "version" || args[1] == "--version" || args[1] == "-v" ||
        wordsEqual(args, 1, {"phiên", "bản"}) || args[1] == "phiên bản") {
        return printVersion(home, release);
    }
    if (args[1] == "doctor" || args[1] == "diagnose" ||
        wordsEqual(args, 1, {"chẩn", "đoán"}) || args[1] == "chẩn đoán") {
        return diagnose(home, release);
    }

    bool libraryList = false;
    std::size_t libraryOptionOffset = 0;
    if (wordsEqual(args, 1, {"thư", "viện", "danh", "sách"})) {
        libraryList = true;
        libraryOptionOffset = 5;
    } else if (args.size() >= 3 && args[1] == "thư viện" &&
               args[2] == "danh sách") {
        libraryList = true;
        libraryOptionOffset = 3;
    } else if (args.size() >= 3 && args[1] == "library" && args[2] == "list") {
        libraryList = true;
        libraryOptionOffset = 3;
    }
    if (libraryList) {
        bool asJson = false;
        for (std::size_t index = libraryOptionOffset; index < args.size(); ++index) {
            if (args[index] == "--json") asJson = true;
        }
        return listLibraries(home, release, asJson);
    }

    return forwardToVpp(home, release, args);
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t *argv[]) {
    try {
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            args.push_back(wideToUtf8(argv[index]));
        }
        return runVdk(std::move(args));
    } catch (const std::exception &error) {
        std::cerr << "VDK: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
#else
int main(int argc, char *argv[]) {
    try {
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) args.emplace_back(argv[index]);
        return runVdk(std::move(args));
    } catch (const std::exception &error) {
        std::cerr << "VDK: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
#endif
