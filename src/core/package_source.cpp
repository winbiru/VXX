#include "vpp/core/package_source.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vpp/core/package_installer.h"
#include "vpp/core/project_layout.h"
#include "vpp/core/semver.h"
#include "vpp/core/text.h"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vietvm::core {
namespace {

namespace fs = std::filesystem;

struct ProcessResult {
    int exitCode = -1;
    std::string output;
};

fs::path absoluteLexical(const fs::path &base, const fs::path &candidate) {
    fs::path resolved = candidate.is_absolute() ? candidate : base / candidate;
    try {
        return fs::absolute(resolved).lexically_normal();
    } catch (...) {
        return resolved.lexically_normal();
    }
}

std::string sourceKey(const std::string &location, const std::string &reference) {
    std::uint64_t hash = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    const std::string input = location + '\n' + reference;
    for (unsigned char byte : input) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string &text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("không thể chuyển đối số Git UTF-8 sang UTF-16");
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            wide.data(), length) != length) {
        throw std::runtime_error("không thể chuyển đối số Git UTF-8 sang UTF-16");
    }
    return wide;
}

void appendWindowsArgument(std::wstring &command, const std::wstring &argument) {
    if (!command.empty()) command.push_back(L' ');
    if (!argument.empty() &&
        argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        command += argument;
        return;
    }

    command.push_back(L'"');
    std::size_t backslashes = 0;
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

ProcessResult runProcess(const std::vector<std::string> &arguments) {
    if (arguments.empty()) throw std::runtime_error("process Git thiếu executable");

    std::wstring command;
    for (const auto &argument : arguments) {
        appendWindowsArgument(command, utf8ToWide(argument));
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &attributes, 0) ||
        !SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        if (readPipe != nullptr) CloseHandle(readPipe);
        if (writePipe != nullptr) CloseHandle(writePipe);
        throw std::runtime_error("không thể tạo pipe cho Git process");
    }

    HANDLE nullInput = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        throw std::runtime_error("không thể mở NUL cho Git process");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const BOOL started = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process);
    CloseHandle(nullInput);
    CloseHandle(writePipe);
    if (!started) {
        CloseHandle(readPipe);
        throw std::runtime_error(
            "không thể khởi động Git; hãy kiểm tra git có trong PATH");
    }

    ProcessResult result;
    char buffer[1024];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr)) {
        if (bytesRead == 0) break;
        result.output.append(buffer, bytesRead);
    }
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) exitCode = 1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.exitCode = static_cast<int>(exitCode);
    return result;
}
#else
ProcessResult runProcess(const std::vector<std::string> &arguments) {
    if (arguments.empty()) throw std::runtime_error("process Git thiếu executable");
    int outputPipe[2];
    if (pipe(outputPipe) != 0) {
        throw std::runtime_error("không thể tạo pipe cho Git process");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(outputPipe[0]);
        close(outputPipe[1]);
        throw std::runtime_error("không thể fork Git process");
    }
    if (pid == 0) {
        (void)dup2(outputPipe[1], STDOUT_FILENO);
        (void)dup2(outputPipe[1], STDERR_FILENO);
        close(outputPipe[0]);
        close(outputPipe[1]);
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto &argument : arguments) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(outputPipe[1]);
    ProcessResult result;
    char buffer[1024];
    ssize_t count = 0;
    while ((count = read(outputPipe[0], buffer, sizeof(buffer))) > 0) {
        result.output.append(buffer, static_cast<std::size_t>(count));
    }
    close(outputPipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        throw std::runtime_error("không thể chờ Git process");
    }
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    }
    return result;
}
#endif

ProcessResult runGit(const std::vector<std::string> &arguments) {
    std::vector<std::string> command;
    command.reserve(arguments.size() + 1);
    command.push_back("git");
    command.insert(command.end(), arguments.begin(), arguments.end());
    return runProcess(command);
}

void requireGitSuccess(const ProcessResult &result, const std::string &operation) {
    if (result.exitCode == 0) return;
    std::string detail = trim(result.output);
    if (result.exitCode == 127 && detail.empty()) {
        detail = "git không có trong PATH";
    }
    throw std::runtime_error(
        operation + " thất bại (exit " + std::to_string(result.exitCode) + ")" +
        (detail.empty() ? "" : ": " + detail));
}

std::string canonicalGitLocation(const std::string &location,
                                 const fs::path &declaringRoot) {
    const fs::path candidate = absoluteLexical(declaringRoot, utf8Path(location));
    if (fs::exists(candidate)) return candidate.generic_u8string();
    return location;
}

MaterializedPackageSource materializeGitSource(
    const PackageDependencySpec &dependency,
    const fs::path &declaringRoot,
    const fs::path &stateRoot) {
    const std::string location =
        canonicalGitLocation(dependency.location, declaringRoot);
    const std::string reference =
        dependency.reference.empty() ? "HEAD" : dependency.reference;
    if (!reference.empty() && reference.front() == '-') {
        throw std::runtime_error(
            "Git ref của dependency '" + dependency.name + "' không hợp lệ: " +
            reference);
    }

    const fs::path checkoutRoot =
        stateRoot / "git" / utf8Path(sourceKey(location, reference));
    std::error_code ec;
    fs::remove_all(checkoutRoot, ec);
    if (ec) {
        throw std::runtime_error(
            "không thể dọn Git checkout cũ: " + checkoutRoot.u8string() +
            " (" + ec.message() + ")");
    }
    fs::create_directories(checkoutRoot.parent_path());

    requireGitSuccess(
        runGit({"clone", "--quiet", "--no-checkout", "--", location,
                checkoutRoot.u8string()}),
        "git clone dependency '" + dependency.name + "'");

    const ProcessResult resolved = runGit(
        {"-C", checkoutRoot.u8string(), "rev-parse", "--verify",
         "--end-of-options", reference + "^{commit}"});
    requireGitSuccess(
        resolved, "git resolve ref '" + reference + "' của dependency '" +
                      dependency.name + "'");
    const std::string revision = trim(resolved.output);
    if (revision.empty()) {
        throw std::runtime_error(
            "Git không trả exact commit cho dependency '" + dependency.name + "'");
    }

    requireGitSuccess(
        runGit({"-C", checkoutRoot.u8string(), "checkout", "--quiet", "--detach",
                revision}),
        "git checkout dependency '" + dependency.name + "'");

    return MaterializedPackageSource{checkoutRoot, revision, location};
}

fs::path findEnclosingRegistryRoot(fs::path start) {
    if (!fs::is_directory(start)) start = start.parent_path();
    for (fs::path current = absoluteLexical(fs::current_path(), start);;) {
        if (fs::exists(current / utf8Path(kRegistryMarkerFile))) return current;
        const fs::path parent = current.parent_path();
        if (parent == current || parent.empty()) break;
        current = parent;
    }
    return {};
}

fs::path resolveRegistryRoot(const PackageDependencySpec &dependency,
                             const fs::path &declaringRoot) {
    fs::path registryRoot;
    if (!dependency.location.empty()) {
        registryRoot = absoluteLexical(
            declaringRoot, utf8Path(dependency.location));
    } else {
        registryRoot = findEnclosingRegistryRoot(declaringRoot);
        if (registryRoot.empty()) {
            if (const char *configured = std::getenv(kEnvVppRegistry)) {
                if (*configured != '\0') {
                    registryRoot = absoluteLexical(
                        fs::current_path(), utf8Path(configured));
                }
            }
        }
    }
    if (registryRoot.empty()) {
        throw std::runtime_error(
            "registry dependency '" + dependency.name +
            "' thiếu location và biến VPP_REGISTRY chưa được cấu hình");
    }
    if (!fs::exists(registryRoot) || !fs::is_directory(registryRoot)) {
        throw std::runtime_error(
            "không tìm thấy registry root cho dependency '" + dependency.name +
            "': " + registryRoot.u8string());
    }
    return registryRoot;
}

struct RegistryCandidate {
    SemanticVersion version;
    std::string versionText;
    fs::path path;
};

MaterializedPackageSource materializeRegistrySource(
    const PackageDependencySpec &dependency,
    const fs::path &declaringRoot) {
    const fs::path registryRoot = resolveRegistryRoot(dependency, declaringRoot);
    const fs::path packageRoot = registryRoot / utf8Path(dependency.name);
    if (!fs::exists(packageRoot) || !fs::is_directory(packageRoot)) {
        throw std::runtime_error(
            "registry không có package '" + dependency.name + "' tại " +
            registryRoot.u8string());
    }

    std::string rangeError;
    const auto range = VersionRange::parse(dependency.versionRange, &rangeError);
    if (!range.has_value()) {
        throw std::runtime_error(
            "registry dependency '" + dependency.name +
            "' có version range không hợp lệ '" + dependency.versionRange +
            "': " + rangeError);
    }

    std::optional<RegistryCandidate> selected;
    for (const auto &entry : fs::directory_iterator(packageRoot)) {
        if (!entry.is_directory()) continue;
        const std::string versionText = entry.path().filename().u8string();
        std::string versionError;
        const auto version = SemanticVersion::parse(versionText, &versionError);
        if (!version.has_value() || !range->matches(*version)) continue;

        const fs::path manifestPath = entry.path() / utf8Path(kProjectManifestFile);
        if (!fs::exists(manifestPath)) {
            throw std::runtime_error(
                "registry package '" + dependency.name + "@" + versionText +
                "' thiếu vpp.json");
        }
        const ProjectManifest manifest = readProjectManifest(manifestPath);
        if (manifest.name != dependency.name || manifest.version != versionText) {
            throw std::runtime_error(
                "registry metadata không khớp layout cho package '" +
                dependency.name + "@" + versionText + "'");
        }

        if (!selected.has_value() ||
            compareSemanticVersion(*version, selected->version) > 0) {
            selected = RegistryCandidate{*version, versionText, entry.path()};
        }
    }

    if (!selected.has_value()) {
        throw std::runtime_error(
            "registry không có version của package '" + dependency.name +
            "' thỏa range " + dependency.versionRange);
    }
    return MaterializedPackageSource{
        selected->path, "", registryRoot.generic_u8string()};
}

void ensureRegistryMarker(const fs::path &registryRoot) {
    fs::create_directories(registryRoot);
    const fs::path marker = registryRoot / utf8Path(kRegistryMarkerFile);
    if (fs::exists(marker)) return;
    std::ofstream output(marker, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error(
            "không thể tạo registry metadata: " + marker.u8string());
    }
    output << "{\n  \"schema\": 1\n}\n";
}

} // namespace

MaterializedPackageSource materializePackageSource(
    const PackageDependencySpec &dependency,
    const std::filesystem::path &declaringRoot,
    const std::filesystem::path &stateRoot) {
    if (dependency.sourceKind == PackageSourceKind::Path) {
        const fs::path sourcePath = absoluteLexical(
            declaringRoot, utf8Path(dependency.location));
        if (!fs::exists(sourcePath)) {
            throw std::runtime_error(
                "không tìm thấy source path của dependency '" + dependency.name +
                "': " + sourcePath.u8string());
        }
        if (!fs::is_directory(sourcePath) && !fs::is_regular_file(sourcePath)) {
            throw std::runtime_error(
                "source path của dependency '" + dependency.name +
                "' không phải file hoặc directory: " + sourcePath.u8string());
        }
        return MaterializedPackageSource{
            sourcePath, "", sourcePath.generic_u8string()};
    }
    if (dependency.sourceKind == PackageSourceKind::Git) {
        return materializeGitSource(dependency, declaringRoot, stateRoot);
    }
    if (dependency.sourceKind == PackageSourceKind::Registry) {
        return materializeRegistrySource(dependency, declaringRoot);
    }
    throw std::runtime_error(
        "dependency '" + dependency.name + "' dùng source '" +
        packageSourceKindName(dependency.sourceKind) +
        "' nhưng transport này chưa được hỗ trợ trong Package 0.9");
}

PublishedRegistryPackage publishPackageToRegistry(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &registryRoot) {
    const fs::path absoluteProject =
        absoluteLexical(fs::current_path(), projectRoot);
    const fs::path absoluteRegistry =
        absoluteLexical(fs::current_path(), registryRoot);
    const fs::path manifestPath = absoluteProject / utf8Path(kProjectManifestFile);
    if (!fs::exists(manifestPath)) {
        throw std::runtime_error(
            "publish: không tìm thấy vpp.json tại " + absoluteProject.u8string());
    }
    const ProjectManifest manifest = readProjectManifest(manifestPath);
    ensureRegistryMarker(absoluteRegistry);

    const fs::path packageRoot = absoluteRegistry / utf8Path(manifest.name);
    const fs::path target = packageRoot / utf8Path(manifest.version);
    fs::create_directories(packageRoot);

    const fs::path staging =
        absoluteRegistry / utf8Path(".vpp-publish-" + manifest.name + "-" +
                                    manifest.version);
    std::error_code ec;
    fs::remove_all(staging, ec);
    const std::string candidateFingerprint =
        materializePathPackage(absoluteProject, staging);

    try {
        if (fs::exists(target)) {
            if (!fs::is_directory(target)) {
                throw std::runtime_error(
                    "registry version path không phải directory: " + target.u8string());
            }
            const std::string existingFingerprint = fingerprintPackageTree(target);
            if (existingFingerprint != candidateFingerprint) {
                throw std::runtime_error(
                    "registry đã có '" + manifest.name + "@" + manifest.version +
                    "' với nội dung khác; version đã publish là bất biến");
            }
            fs::remove_all(staging, ec);
            return PublishedRegistryPackage{
                manifest.name, manifest.version, target, existingFingerprint, true};
        }

        fs::rename(staging, target);
        return PublishedRegistryPackage{
            manifest.name, manifest.version, target, candidateFingerprint, false};
    } catch (...) {
        fs::remove_all(staging, ec);
        throw;
    }
}

} // namespace vietvm::core
