#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>
#include "../../include/compiler/compiler.h"
#include "../../include/vm/vm.h"
#include "../../include/frontend/keywords.h"
#include "common/storeString.h"
#include "../../include/compiler/compileRegistry.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/tooling/tooling.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/project_layout.h"
#include "vpp/core/text.h"

namespace fs = std::filesystem;
namespace messages = vietvm::messages;

// RAII guard to restore current working directory on scope exit
struct CwdGuard {
    fs::path saved;
    explicit CwdGuard(fs::path p) : saved(std::move(p)) {}
    ~CwdGuard() { try { fs::current_path(saved); } catch(...) {} }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;
};

static void printErrorMessage(const messages::MessageDefinition &fallback,
                              const std::string &detail) {
    if (messages::hasMessageCode(detail)) {
        std::cerr << detail << std::endl;
        return;
    }
    std::cerr << messages::formatMessage(fallback, {detail}) << std::endl;
}

std::string readFile(const std::string &filename) {
    std::ifstream fileStream(vietvm::core::utf8Path(filename));
    if (!fileStream.is_open()) {
        throw std::runtime_error(
            messages::formatMessage(messages::kCliFileOpenFailed, {filename}));
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();
    return buffer.str();
}

static void printUsage() {
    std::cout << messages::messageText(messages::kCliUsage);
}

static void printVersion() {
    std::cout << messages::messageText(messages::kCliVersion,
                                       {vietvm::core::kCliVersion});
}

enum class SnippetMode {
    Execute,
    Disassemble,
    DumpAst,
    DumpIr,
};

static int runSnippet(const std::string &source,
                      const fs::path &cwd,
                      SnippetMode mode) {
    CwdGuard cwdGuard(fs::current_path());
    if (!cwd.empty()) {
        fs::current_path(cwd);
    }

    vietvm::compiler::resetCompilationState();
    const bool emitMainCall = mode == SnippetMode::Execute ||
                              mode == SnippetMode::Disassemble;
    vietvm::compiler::CompilationArtifacts artifacts =
        vietvm::compiler::compilePipeline(source, keywordMap, emitMainCall);
    const auto &stringPool = vietvm::compiler::StringPool::getPool();

    switch (mode) {
        case SnippetMode::DumpAst:
            std::cout << vietvm::tooling::dumpAst(artifacts.ast);
            return EXIT_SUCCESS;
        case SnippetMode::DumpIr:
            // compilePipeline returns the post-optimizer IR, so this is the
            // representation used for direct-emitter/bridge selection.
            std::cout << "backend="
                      << vietvm::compiler::bytecodeBackendName(artifacts.backend)
                      << " codegen-fallback-regions="
                      << artifacts.legacyFallbackRegions << '\n';
            std::cout << vietvm::tooling::dumpIr(artifacts.ir);
            return EXIT_SUCCESS;
        case SnippetMode::Disassemble:
            std::cout << vietvm::tooling::disassembleBytecode(artifacts.bytecode, stringPool);
            return EXIT_SUCCESS;
        case SnippetMode::Execute:
            break;
    }

    VM vm(artifacts.bytecode, stringPool);
    vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
    for (const auto &entry : vietvm::compiler::hamMap::hamNameIndexMap) {
        vm.functionTableByNameIndex[entry.second] = entry.first;
    }
    vm.run();
    return EXIT_SUCCESS;
}

static int runFile(const std::string &filename,
                   SnippetMode mode,
                   bool lintOnly = false) {
    std::string source = readFile(filename);
    fs::path filePath = vietvm::core::utf8Path(filename);
    fs::path fileDir = filePath.parent_path();

    if (lintOnly) {
        std::string errorMessage;
        if (vietvm::tooling::lintSource(source, errorMessage)) {
            std::cout << messages::messageText(messages::kToolLintPassed, {filename});
            return EXIT_SUCCESS;
        }
        std::cerr << messages::formatMessage(messages::kToolLintFailed,
                                             {filename, errorMessage}) << std::endl;
        return EXIT_FAILURE;
    }

    return runSnippet(source, fileDir.empty() ? fs::current_path() : fileDir, mode);
}

static int runRepl() {
    std::cout << messages::messageText(messages::kReplWelcome);
    std::string line;
    while (true) {
        std::cout << messages::messageText(messages::kReplPrompt);
        if (!std::getline(std::cin, line)) break;

        const std::string trimmed = vietvm::core::trim(line);
        if (trimmed.empty()) continue;
        if (trimmed == ":quit" || trimmed == ":exit") break;
        if (trimmed == ":help") {
            std::cout << messages::messageText(messages::kReplHelp);
            continue;
        }

        std::string source = "nhập \"stdlib\";\n" + line;
        try {
            (void)runSnippet(source, fs::current_path(), SnippetMode::Execute);
        } catch (const std::exception &ex) {
            printErrorMessage(messages::kReplExecutionFailed, ex.what());
        }
    }
    return EXIT_SUCCESS;
}

static fs::path packageManifestPath(const fs::path &root) {
    return root / vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);
}

static fs::path packageRootPath(const fs::path &root) {
    for (const char *directoryName : vietvm::core::kPackageDirectoryNames) {
        const fs::path candidate = root / vietvm::core::utf8Path(directoryName);
        if (fs::exists(candidate)) return candidate;
    }
    return root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory);
}

static std::vector<std::string> listPackages(const fs::path &root) {
    std::vector<std::string> packages;
    fs::path packagesDir = packageRootPath(root);
    if (!fs::exists(packagesDir)) return packages;
    for (const auto &entry : fs::directory_iterator(packagesDir)) {
        if (entry.is_directory()) {
            fs::path mainFile = vietvm::core::packageEntryPath(entry.path());
            if (fs::exists(mainFile)) {
                packages.push_back(entry.path().filename().u8string());
            }
        }
    }
    std::sort(packages.begin(), packages.end());
    return packages;
}

static void writePackageManifest(const fs::path &root, const std::string &name) {
    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"name\": \"" << name << "\",\n"
             << "  \"version\": \"" << vietvm::core::kCliVersion << "\",\n"
             << "  \"gói\": [";
    auto packages = listPackages(root);
    for (size_t i = 0; i < packages.size(); ++i) {
        if (i > 0) manifest << ", ";
        manifest << "\"" << packages[i] << "\"";
    }
    manifest << "]\n}";
    std::ofstream out(packageManifestPath(root));
    out << manifest.str() << std::endl;
}

static int pkgInit(const std::string &name) {
    fs::path root = fs::current_path();
    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    if (name.empty()) {
        writePackageManifest(root, root.filename().u8string());
    } else {
        writePackageManifest(root, name);
    }
    std::cout << messages::messageText(messages::kPkgManifestCreated,
                                       {packageManifestPath(root).u8string()});
    return EXIT_SUCCESS;
}

static int backendInit(const std::string &name) {
    if (name.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgBackendNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::path root = fs::current_path() / vietvm::core::utf8Path(name);
    if (fs::exists(root)) {
        std::cerr << messages::formatMessage(messages::kPkgBackendDirectoryExists,
                                             {root.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    fs::path templateRoot;
    if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
        fs::path candidate = fs::u8path(vppHome) / "templates" / "backend";
        if (fs::exists(candidate)) templateRoot = candidate;
    }
    if (templateRoot.empty()) {
        for (fs::path dir = fs::current_path(); ; dir = dir.parent_path()) {
            fs::path candidate = dir / "templates" / "backend";
            if (fs::exists(candidate)) {
                templateRoot = candidate;
                break;
            }
            if (dir == dir.parent_path()) break;
        }
    }
    if (templateRoot.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgBackendTemplatesMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    writePackageManifest(root, name);
    for (const char *filename : {"application.vi", "application.properties", "README.md", ".gitignore"}) {
        std::error_code ec;
        fs::copy_file(templateRoot / filename, root / filename, fs::copy_options::none, ec);
        if (ec) {
            std::cerr << messages::formatMessage(messages::kPkgBackendTemplateCopyFailed,
                                                 {filename, ec.message()}) << '\n';
            return EXIT_FAILURE;
        }
    }

    std::cout << messages::messageText(messages::kPkgBackendCreated, {root.u8string()});
    return EXIT_SUCCESS;
}

static int pkgAdd(const std::string &sourceArg, const std::string &packageNameArg) {
    fs::path sourcePath = vietvm::core::utf8Path(sourceArg);
    if (!fs::exists(sourcePath)) {
        std::cerr << messages::formatMessage(messages::kPkgSourceNotFound, {sourceArg}) << std::endl;
        return EXIT_FAILURE;
    }

    fs::path packageRoot = packageRootPath(fs::current_path());
    fs::create_directories(packageRoot);

    std::string packageName = packageNameArg;
    if (packageName.empty()) {
        packageName = sourcePath.filename().u8string();
        if (sourcePath.has_extension()) {
            packageName = sourcePath.stem().u8string();
        }
    }

    fs::path targetDir = packageRoot / vietvm::core::utf8Path(packageName);
    fs::create_directories(targetDir);

    if (fs::is_directory(sourcePath)) {
        for (const auto &entry : fs::recursive_directory_iterator(sourcePath)) {
            fs::path relative = fs::relative(entry.path(), sourcePath);
            fs::path target = targetDir / relative;
            if (entry.is_directory()) {
                fs::create_directories(target);
            } else if (entry.is_regular_file()) {
                fs::create_directories(target.parent_path());
                fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
            }
        }
    } else {
        fs::path target = vietvm::core::packageEntryPath(targetDir);
        fs::copy_file(sourcePath, target, fs::copy_options::overwrite_existing);
    }

    writePackageManifest(fs::current_path(), fs::current_path().filename().u8string());
    std::cout << messages::messageText(messages::kPkgInstalled, {packageName});
    return EXIT_SUCCESS;
}

static int pkgList() {
    auto packages = listPackages(fs::current_path());
    if (packages.empty()) {
        std::cout << messages::messageText(messages::kPkgListEmpty);
        return EXIT_SUCCESS;
    }
    for (const auto &pkg : packages) {
        std::cout << pkg << '\n';
    }
    return EXIT_SUCCESS;
}

static int pkgRemove(const std::string &packageName) {
    if (packageName.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgRemoveNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::path targetDir = packageRootPath(fs::current_path()) /
                         vietvm::core::utf8Path(packageName);
    if (!fs::exists(targetDir)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    std::error_code ec;
    fs::remove_all(targetDir, ec);
    if (ec) {
        std::cerr << messages::formatMessage(messages::kPkgRemoveFailed,
                                             {packageName, ec.message()}) << '\n';
        return EXIT_FAILURE;
    }

    writePackageManifest(fs::current_path(), fs::current_path().filename().u8string());
    std::cout << messages::messageText(messages::kPkgRemoved, {packageName});
    return EXIT_SUCCESS;
}

static bool packageExists(const fs::path &root, const std::string &packageName) {
    if (packageName.empty()) return false;
    fs::path packageMain = vietvm::core::packageEntryPath(
        packageRootPath(root) / vietvm::core::utf8Path(packageName));
    return fs::exists(packageMain);
}

static int pkgInfo(const std::string &packageName) {
    if (packageName.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgInfoNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::path root = fs::current_path();
    fs::path packageDir = packageRootPath(root) / vietvm::core::utf8Path(packageName);
    fs::path mainFile = vietvm::core::packageEntryPath(packageDir);

    if (!fs::exists(packageDir)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << messages::messageText(messages::kPkgInfoName, {packageName});
    std::cout << messages::messageText(messages::kPkgInfoPath, {packageDir.u8string()});
    std::cout << messages::messageText(
        messages::kPkgInfoMainFile,
        {fs::exists(mainFile) ? mainFile.u8string()
                              : messages::messageText(messages::kPkgValueAbsent)});
    return EXIT_SUCCESS;
}

static int pkgHas(const std::string &packageName) {
    bool exists = packageExists(fs::current_path(), packageName);
    std::cout << messages::messageText(exists ? messages::kPkgValuePresent
                                               : messages::kPkgValueAbsent) << std::endl;
    return exists ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int pkgStats() {
    fs::path root = fs::current_path();
    auto packages = listPackages(root);
    fs::path manifestPath = root / vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);

    std::cout << messages::messageText(messages::kPkgStatsProject, {root.filename().u8string()});
    std::cout << messages::messageText(
        messages::kPkgStatsManifest,
        {messages::messageText(fs::exists(manifestPath) ? messages::kPkgValuePresent
                                                         : messages::kPkgValueAbsent)});
    std::cout << messages::messageText(messages::kPkgStatsCount,
                                       {std::to_string(packages.size())});
    return EXIT_SUCCESS;
}

static int runPackageCommand(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << messages::formatMessage(messages::kPkgSubcommandMissing) << '\n';
        return EXIT_FAILURE;
    }
    std::string sub = argv[2];
    int subWordOffset = 0;
    if (argc >= 4) {
        std::string twoWordSub = sub + " " + std::string(argv[3]);
        if (twoWordSub == "khởi tạo" || twoWordSub == "danh sách" ||
            twoWordSub == "thông tin" || twoWordSub == "kiểm tra" ||
            twoWordSub == "cài đặt") {
            sub = twoWordSub;
            subWordOffset = 1;
        }
    }
    if (sub == "init" || sub == "khởi tạo") {
        std::string name = (argc >= (4 + subWordOffset)) ? argv[3 + subWordOffset] : "";
        return pkgInit(name);
    }
    if (sub == "add" || sub == "thêm") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgAddSourceMissing) << '\n';
            return EXIT_FAILURE;
        }
        std::string sourceArg = argv[3 + subWordOffset];
        std::string name = (argc >= (5 + subWordOffset)) ? argv[4 + subWordOffset] : "";
        return pkgAdd(sourceArg, name);
    }
    if (sub == "list" || sub == "danh sách") {
        return pkgList();
    }
    if (sub == "remove" || sub == "xóa") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgRemoveNameMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgRemove(argv[3 + subWordOffset]);
    }
    if (sub == "info" || sub == "thông tin") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgInfoNameMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgInfo(argv[3 + subWordOffset]);
    }
    if (sub == "has" || sub == "kiểm tra") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgHasNameMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgHas(argv[3 + subWordOffset]);
    }
    if (sub == "stats" || sub == "thống kê") {
        return pkgStats();
    }
    std::cerr << messages::formatMessage(messages::kPkgInvalidSubcommand, {sub}) << std::endl;
    return EXIT_FAILURE;
}

static int runDoctor(const std::string &execPath) {
    std::cout << messages::messageText(messages::kCliDoctorHeading);
    std::cout << messages::messageText(messages::kCliDoctorVersion,
                                       {vietvm::core::kCliVersion});
    std::cout << messages::messageText(messages::kCliDoctorExecutable, {execPath});
    std::cout << messages::messageText(messages::kCliDoctorCurrentDirectory,
                                       {fs::current_path().u8string()});

    fs::path manifestPath = fs::current_path() /
                            vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);
    std::cout << messages::messageText(
        messages::kCliDoctorManifest,
        {messages::messageText(fs::exists(manifestPath) ? messages::kPkgValuePresent
                                                         : messages::kPkgValueAbsent)});

    auto packages = listPackages(fs::current_path());
    std::cout << messages::messageText(messages::kCliDoctorPackageCount,
                                       {std::to_string(packages.size())});
    return EXIT_SUCCESS;
}

static std::string jsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string jsonUnescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: out.push_back(n); break;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

static std::optional<std::string> readLspMessage() {
    std::string line;
    int contentLength = -1;
    while (std::getline(std::cin, line)) {
        if (line == "\r" || line.empty()) break;
        std::string normalized = vietvm::core::trim(line);
        const std::string prefix = "Content-Length:";
        if (normalized.rfind(prefix, 0) == 0) {
            contentLength = std::stoi(vietvm::core::trim(normalized.substr(prefix.size())));
        }
    }
    if (contentLength < 0) return std::nullopt;

    std::string body(contentLength, '\0');
    std::cin.read(body.data(), contentLength);
    if (!std::cin) return std::nullopt;
    return body;
}

static std::string extractJsonStringField(const std::string &body, const std::string &field) {
    std::regex rx("\"" + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
    std::smatch match;
    if (std::regex_search(body, match, rx) && match.size() > 1) {
        return jsonUnescape(match[1].str());
    }
    return "";
}

static std::string extractJsonRawField(const std::string &body, const std::string &field) {
    std::regex rx("\"" + field + "\"\\s*:\\s*([^,}]+)");
    std::smatch match;
    if (std::regex_search(body, match, rx) && match.size() > 1) {
        return vietvm::core::trim(match[1].str());
    }
    return "";
}

static void writeLspMessage(const std::string &payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush;
}

static void publishDiagnostics(const std::string &uri, const std::string &text) {
    std::string errorMessage;
    std::string result = "[]";
    if (!vietvm::tooling::lintSource(text, errorMessage)) {
        const std::string diagnosticCode(
            messages::messageCodeFromFormatted(errorMessage));
        std::ostringstream diag;
        diag << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
             << "\"severity\":1,\"source\":\"vpp\"";
        if (!diagnosticCode.empty()) {
            diag << ",\"code\":\"" << jsonEscape(diagnosticCode) << "\"";
        }
        diag << ",\"message\":\"" << jsonEscape(errorMessage) << "\"}]";
        result = diag.str();
    }

    std::ostringstream notif;
    notif << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
          << "\"uri\":\"" << jsonEscape(uri) << "\","
          << "\"diagnostics\":" << result << "}}";
    writeLspMessage(notif.str());
}

static int runLanguageServer() {
    std::unordered_map<std::string, std::string> openDocuments;
    bool shutdownRequested = false;

    while (true) {
        auto payload = readLspMessage();
        if (!payload.has_value()) break;
        const std::string &body = payload.value();
        std::string method = extractJsonStringField(body, "method");
        std::string id = extractJsonRawField(body, "id");

        if (method == "initialize") {
            std::ostringstream response;
            response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                     << ",\"result\":{\"capabilities\":{"
                     << "\"textDocumentSync\":1,"
                     << "\"documentFormattingProvider\":true,"
                     << "\"definitionProvider\":true,"
                     << "\"hoverProvider\":true"
                     << "}}}";
            writeLspMessage(response.str());
            continue;
        }

        if (method == "shutdown") {
            shutdownRequested = true;
            std::ostringstream response;
            response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                     << ",\"result\":null}";
            writeLspMessage(response.str());
            continue;
        }

        if (method == "exit") {
            break;
        }

        if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            std::string uri = extractJsonStringField(body, "uri");
            std::string text = extractJsonStringField(body, "text");
            if (!uri.empty()) {
                openDocuments[uri] = text;
                publishDiagnostics(uri, text);
            }
            continue;
        }
    }

    return shutdownRequested ? EXIT_SUCCESS : EXIT_FAILURE;
}


int main(int argc, char* argv[]) {
    try {
        initCompileMap();

        if (argc >= 2) {
            std::string command = argv[1];
            int commandWordOffset = 0;
            if (argc >= 3) {
                std::string twoWordCommand = command + " " + std::string(argv[2]);
                if (twoWordCommand == "giúp đỡ" || twoWordCommand == "phiên bản" ||
                    twoWordCommand == "bác sĩ" || twoWordCommand == "danh sách" ||
                    twoWordCommand == "khởi tạo" || twoWordCommand == "thông tin" ||
                    twoWordCommand == "kiểm tra" || twoWordCommand == "thống kê" ||
                    twoWordCommand == "cài đặt" || twoWordCommand == "--giải mã" ||
                    twoWordCommand == "--định dạng") {
                    command = twoWordCommand;
                    commandWordOffset = 1;
                }
            }
            if (command == "--help" || command == "-h" || command == "help" || command == "commands" || command == "giúp đỡ") {
                printUsage();
                return EXIT_SUCCESS;
            }
            if (command == "--version" || command == "-v" || command == "version" || command == "phiên bản") {
                printVersion();
                return EXIT_SUCCESS;
            }
            if (command == "doctor" || command == "bác sĩ") {
                return runDoctor(argv[0]);
            }
            if (command == "where" || command == "nơi") {
                std::cout << fs::current_path().u8string() << std::endl;
                return EXIT_SUCCESS;
            }
            if (command == "--lsp") {
                return runLanguageServer();
            }
            if (command == "--repl") {
                return runRepl();
            }
            if (command == "run" || command == "chạy") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliRunMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::Execute);
            }
            if (command == "--disassemble" || command == "--giải mã" || command == "--giải-mã") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliDisassembleMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::Disassemble);
            }
            if (command == "--dump-ast") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliDumpAstMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::DumpAst);
            }
            if (command == "--dump-ir") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliDumpIrMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::DumpIr);
            }
            if (command == "--lint") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliLintMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::Execute, true);
            }
            if (command == "--format" || command == "--định dạng" || command == "--định-dạng") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliFormatMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                std::string filename = argv[2 + commandWordOffset];
                bool inPlace = false;
                for (int i = 3 + commandWordOffset; i < argc; ++i) {
                    if (std::string(argv[i]) == "--in-place") inPlace = true;
                }
                std::string source = readFile(filename);
                std::string formatted = vietvm::tooling::formatSource(source);
                if (inPlace) {
                    std::ofstream out(filename);
                    out << formatted;
                } else {
                    std::cout << formatted;
                }
                return EXIT_SUCCESS;
            }
            if (command == "pkg" || command == "gói") {
                return runPackageCommand(argc, argv);
            }
            if (command == "init" || command == "khởi tạo") {
                std::string name = (argc >= (3 + commandWordOffset)) ? argv[2 + commandWordOffset] : "";
                if (name == "backend") {
                    std::string backendName = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                    return backendInit(backendName);
                }
                return pkgInit(name);
            }
            if (command == "install" || command == "cai" || command == "caidat" || command == "cài đặt") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgInstallSourceMissing) << '\n';
                    return EXIT_FAILURE;
                }
                std::string sourceArg = argv[2 + commandWordOffset];
                std::string name = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                return pkgAdd(sourceArg, name);
            }
            if (command == "list" || command == "danh sách") {
                return pkgList();
            }
            if (command == "remove" || command == "xoa" || command == "xóa") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgTopLevelRemoveNameMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgRemove(argv[2 + commandWordOffset]);
            }
            if (command == "info" || command == "thông tin") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgTopLevelInfoNameMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgInfo(argv[2 + commandWordOffset]);
            }
            if (command == "has" || command == "kiểm tra") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgTopLevelHasNameMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgHas(argv[2 + commandWordOffset]);
            }
            if (command == "stats" || command == "thống kê") {
                return pkgStats();
            }
        }

        // -----------------------------
        // Trường hợp có đối số (chạy file được chỉ định)
        // -----------------------------
        if (argc == 2) {
            return runFile(argv[1], SnippetMode::Execute);
        }

        // -----------------------------
        // Nếu không có đối số → chạy file mặc định
        // -----------------------------
        const std::string defaultFile = "../../src/tests/kiem_tra_stdlib_tinh_toan.vi";
        if (argc == 1 && fs::exists(defaultFile)) {
            std::string source = readFile(defaultFile);

            // run default file with cwd set to its parent so imports resolve
            // Use RAII-style guard to always restore cwd even on exception
            CwdGuard cwdGuard(fs::current_path());
            const fs::path defaultPath = vietvm::core::utf8Path(defaultFile);
            if (!defaultPath.parent_path().empty()) {
                fs::current_path(defaultPath.parent_path());
            }

            vietvm::compiler::resetCompilationState();

            std::vector<Instruction> bytecode = compileSource(source, keywordMap);
            // cwd will be restored by CwdGuard destructor
            const auto& stringPool = vietvm::compiler::StringPool::getPool();

            // // In bytecode để debug
            // std::cout << "=> Danh sách bytecode cho file mã nguồn (" << defaultFile << "):" << std::endl;
            // for (size_t i = 0; i < bytecode.size(); ++i) {
            //     const Instruction &instr = bytecode[i];
            //     std::cout << "[" << i << "] "
            //               << "op: " << instr.op << " (" << name_op(instr.op) << ")";
            //     if (instr.operandIndex != -1)
            //         std::cout << ", operandIndex: " << instr.operandIndex;
            //     if (instr.operand != 0)
            //         std::cout << ", operand: " << instr.operand;
            //     std::cout << std::endl;
            // }
            VM vm(bytecode, stringPool);

            // copy compiled functions into VM
            vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
            vm.run();
            return EXIT_SUCCESS;
        }

        std::string testDir = "../../src/tests";
        if (fs::exists(testDir)) {
            for (const auto& entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".vi") {
                    const std::string filename = entry.path().u8string();
                    std::cout << messages::messageText(messages::kCliTestRunning, {filename})
                              << std::endl;

                    std::string source = readFile(filename);
                    // Ensure imports inside each test file resolve relative to the test file location
                    // Use RAII-style guard to always restore cwd even on exception
                    CwdGuard cwdGuard(fs::current_path());
                    const fs::path testPath = vietvm::core::utf8Path(filename);
                    if (!testPath.parent_path().empty()) {
                        fs::current_path(testPath.parent_path());
                    }

                    vietvm::compiler::resetCompilationState();
                    std::vector<Instruction> bytecode = compileSource(source, keywordMap);
                    // cwd will be restored by CwdGuard destructor
                    const auto& stringPool = vietvm::compiler::StringPool::getPool();

                    VM vm(bytecode, stringPool);
                    // copy compiled functions into VM
                    vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
                    vm.run();
                }
            }
        } else {
            std::cerr << messages::formatMessage(messages::kCliTestsDirectoryMissing) << '\n';
        }

    } catch (const std::exception &ex) {
        printErrorMessage(messages::kCliUnhandledException, ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
