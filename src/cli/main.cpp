#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <unordered_map>
#include <filesystem>
#include "../../include/compiler/compiler.h"
#include "../../include/vm/vm.h"
#include "../../include/frontend/keywords.h"
#include "common/storeString.h"
#include "../../include/compiler/compileRegistry.h"
#include "../../include/common/tooling.h"

namespace fs = std::filesystem;
static constexpr const char* kVppCliVersion = "0.1.0";

// RAII guard to restore current working directory on scope exit
struct CwdGuard {
    fs::path saved;
    explicit CwdGuard(fs::path p) : saved(std::move(p)) {}
    ~CwdGuard() { try { fs::current_path(saved); } catch(...) {} }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;
};

std::string readFile(const std::string &filename) {
    std::ifstream fileStream(filename);
    if (!fileStream.is_open()) {
        throw std::runtime_error("Không thể mở file: " + filename);
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();
    return buffer.str();
}

static void resetCompilerState() {
    vietvm::compiler::StringPool::clear();
    vietvm::compiler::clearImportedFiles();
    vietvm::compiler::hamMap::hamBytecodeMap.clear();
    vietvm::compiler::hamMap::clearHamNameIndexMap();
    vietvm::compiler::hamMap::resetHamIdCounter();
}

static void printUsage() {
    std::cout
        << "V++ CLI\n"
        << "Cách dùng:\n"
        << "  vpp giúp đỡ\n"
        << "  vpp phiên bản\n"
        << "  vpp bác sĩ\n"
        << "  vpp nơi\n"
        << "  vpp thống kê\n"
        << "  vpp <file.vi>\n"
        << "  vpp chạy <file.vi>\n"
        << "  vpp --giải-mã <file.vi>\n"
        << "  vpp --lint <file.vi>\n"
        << "  vpp --định-dạng <file.vi> [--in-place]\n"
        << "  vpp --repl\n"
        << "  vpp khởi tạo [tên-dự-án]\n"
        << "  vpp cài đặt <nguồn> [tên]\n"
        << "  vpp danh sách\n"
        << "  vpp thông tin <tên>\n"
        << "  vpp kiểm tra <tên>\n"
        << "  vpp pkg khởi tạo [tên]\n"
        << "  vpp pkg thêm <nguồn> [tên]\n"
        << "  vpp pkg xóa <tên>\n"
        << "  vpp pkg danh sách\n"
        << "  vpp pkg thông tin <tên>\n"
        << "  vpp pkg kiểm tra <tên>\n";
}

static void printVersion() {
    std::cout << "Phiên bản V++ CLI: " << kVppCliVersion << '\n';
}

static int runSnippet(const std::string &source, const fs::path &cwd, bool execute, bool dumpBytecode) {
    CwdGuard cwdGuard(fs::current_path());
    if (!cwd.empty()) {
        fs::current_path(cwd);
    }

    resetCompilerState();
    std::vector<Instruction> bytecode = compileSource(source, keywordMap);
    const auto &stringPool = vietvm::compiler::StringPool::getPool();

    if (dumpBytecode) {
        std::cout << vietvm::tooling::disassembleBytecode(bytecode, stringPool);
        return EXIT_SUCCESS;
    }

    if (!execute) {
        return EXIT_SUCCESS;
    }

    VM vm(bytecode, stringPool);
    vm.hamBytecodeMap = vietvm::compiler::hamMap::hamBytecodeMap;
    vm.run();
    return EXIT_SUCCESS;
}

static int runFile(const std::string &filename, bool dumpBytecode, bool lintOnly) {
    std::string source = readFile(filename);
    fs::path filePath(filename);
    fs::path fileDir = filePath.parent_path();

    if (lintOnly) {
        std::string errorMessage;
        if (vietvm::tooling::lintSource(source, errorMessage)) {
            std::cout << filename << ": OK\n";
            return EXIT_SUCCESS;
        }
        std::cerr << filename << ": " << errorMessage << std::endl;
        return EXIT_FAILURE;
    }

    return runSnippet(source, fileDir.empty() ? fs::current_path() : fileDir, true, dumpBytecode);
}

static int runRepl() {
    std::cout << "V++ REPL. Nhập :quit để thoát.\n";
    std::string line;
    while (true) {
        std::cout << "vpp> ";
        if (!std::getline(std::cin, line)) break;

        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = trimmed.find_last_not_of(" \t\r\n");
        trimmed = trimmed.substr(start, end - start + 1);
        if (trimmed.empty()) continue;
        if (trimmed == ":quit" || trimmed == ":exit") break;
        if (trimmed == ":help") {
            std::cout << ":quit, :exit, :help\n";
            continue;
        }

        std::string source = "nhập \"stdlib\";\n" + line;
        try {
            (void)runSnippet(source, fs::current_path(), true, false);
        } catch (const std::exception &ex) {
            std::cerr << "Lỗi: " << ex.what() << std::endl;
        }
    }
    return EXIT_SUCCESS;
}

static std::string packageManifestPath(const fs::path &root) {
    return (root / "vpp.json").string();
}

static std::vector<std::string> listPackages(const fs::path &root) {
    std::vector<std::string> packages;
    fs::path packagesDir = root / "packages";
    if (!fs::exists(packagesDir)) return packages;
    for (const auto &entry : fs::directory_iterator(packagesDir)) {
        if (entry.is_directory()) {
            fs::path mainFile = entry.path() / "main.vi";
            if (fs::exists(mainFile)) {
                packages.push_back(entry.path().filename().string());
            }
        }
    }
    return packages;
}

static void writePackageManifest(const fs::path &root, const std::string &name) {
    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"name\": \"" << name << "\",\n"
             << "  \"version\": \"0.1.0\",\n"
             << "  \"packages\": [";
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
    fs::create_directories(root / "packages");
    if (name.empty()) {
        writePackageManifest(root, root.filename().string());
    } else {
        writePackageManifest(root, name);
    }
    std::cout << "Da tao manifest tai " << packageManifestPath(root) << std::endl;
    return EXIT_SUCCESS;
}

static int pkgAdd(const std::string &sourceArg, const std::string &packageNameArg) {
    fs::path sourcePath = fs::path(sourceArg);
    if (!fs::exists(sourcePath)) {
        std::cerr << "Khong tim thay nguon: " << sourceArg << std::endl;
        return EXIT_FAILURE;
    }

    fs::path packageRoot = fs::current_path() / "packages";
    fs::create_directories(packageRoot);

    std::string packageName = packageNameArg;
    if (packageName.empty()) {
        packageName = sourcePath.filename().string();
        if (sourcePath.has_extension()) {
            packageName = sourcePath.stem().string();
        }
    }

    fs::path targetDir = packageRoot / packageName;
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
        fs::path target = targetDir / "main.vi";
        fs::copy_file(sourcePath, target, fs::copy_options::overwrite_existing);
    }

    writePackageManifest(fs::current_path(), fs::current_path().filename().string());
    std::cout << "Da cai goi: " << packageName << std::endl;
    return EXIT_SUCCESS;
}

static int pkgList() {
    auto packages = listPackages(fs::current_path());
    if (packages.empty()) {
        std::cout << "Chua co goi nao duoc cai.\n";
        return EXIT_SUCCESS;
    }
    for (const auto &pkg : packages) {
        std::cout << pkg << '\n';
    }
    return EXIT_SUCCESS;
}

static int pkgRemove(const std::string &packageName) {
    if (packageName.empty()) {
        std::cerr << "pkg xoa: thieu ten goi\n";
        return EXIT_FAILURE;
    }

    fs::path targetDir = fs::current_path() / "packages" / packageName;
    if (!fs::exists(targetDir)) {
        std::cerr << "Khong tim thay goi: " << packageName << std::endl;
        return EXIT_FAILURE;
    }

    std::error_code ec;
    fs::remove_all(targetDir, ec);
    if (ec) {
        std::cerr << "Khong the xoa goi: " << packageName << " (" << ec.message() << ")\n";
        return EXIT_FAILURE;
    }

    writePackageManifest(fs::current_path(), fs::current_path().filename().string());
    std::cout << "Da xoa goi: " << packageName << std::endl;
    return EXIT_SUCCESS;
}

static bool packageExists(const fs::path &root, const std::string &packageName) {
    if (packageName.empty()) return false;
    fs::path packageMain = root / "packages" / packageName / "main.vi";
    return fs::exists(packageMain);
}

static int pkgInfo(const std::string &packageName) {
    if (packageName.empty()) {
        std::cerr << "pkg thong tin: thieu ten goi\n";
        return EXIT_FAILURE;
    }

    fs::path root = fs::current_path();
    fs::path packageDir = root / "packages" / packageName;
    fs::path mainFile = packageDir / "main.vi";

    if (!fs::exists(packageDir)) {
        std::cerr << "Khong tim thay goi: " << packageName << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "ten: " << packageName << '\n';
    std::cout << "duong_dan: " << packageDir.string() << '\n';
    std::cout << "tep_chinh: " << (fs::exists(mainFile) ? mainFile.string() : "khong") << '\n';
    return EXIT_SUCCESS;
}

static int pkgHas(const std::string &packageName) {
    bool exists = packageExists(fs::current_path(), packageName);
    std::cout << (exists ? "co" : "khong") << std::endl;
    return exists ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int pkgStats() {
    fs::path root = fs::current_path();
    auto packages = listPackages(root);
    fs::path manifestPath = root / "vpp.json";

    std::cout << "du_an: " << root.filename().string() << '\n';
    std::cout << "manifest: " << (fs::exists(manifestPath) ? "co" : "khong") << '\n';
    std::cout << "so_goi: " << packages.size() << '\n';
    return EXIT_SUCCESS;
}

static int runPackageCommand(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "pkg: thieu lenh con\n";
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
            std::cerr << "pkg them: thieu duong dan nguon\n";
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
            std::cerr << "pkg xoa: thieu ten goi\n";
            return EXIT_FAILURE;
        }
        return pkgRemove(argv[3 + subWordOffset]);
    }
    if (sub == "info" || sub == "thông tin") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << "pkg thong tin: thieu ten goi\n";
            return EXIT_FAILURE;
        }
        return pkgInfo(argv[3 + subWordOffset]);
    }
    if (sub == "has" || sub == "kiểm tra") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << "pkg kiem tra: thieu ten goi\n";
            return EXIT_FAILURE;
        }
        return pkgHas(argv[3 + subWordOffset]);
    }
    if (sub == "stats" || sub == "thống kê") {
        return pkgStats();
    }
    std::cerr << "pkg: lenh con khong hop le: " << sub << std::endl;
    return EXIT_FAILURE;
}

static int runDoctor(const std::string &execPath) {
    std::cout << "Chan doan V++ CLI\n";
    std::cout << "  phien_ban: " << kVppCliVersion << '\n';
    std::cout << "  tep_thuc_thi: " << execPath << '\n';
    std::cout << "  thu_muc_hien_tai: " << fs::current_path().string() << '\n';

    fs::path manifestPath = fs::current_path() / "vpp.json";
    std::cout << "  manifest: " << (fs::exists(manifestPath) ? "co" : "khong") << '\n';

    auto packages = listPackages(fs::current_path());
    std::cout << "  so_goi: " << packages.size() << '\n';
    return EXIT_SUCCESS;
}

static std::string trimCopy(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
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
        std::string normalized = trimCopy(line);
        const std::string prefix = "Content-Length:";
        if (normalized.rfind(prefix, 0) == 0) {
            contentLength = std::stoi(trimCopy(normalized.substr(prefix.size())));
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
        return trimCopy(match[1].str());
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
        std::ostringstream diag;
        diag << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
             << "\"severity\":1,\"source\":\"vpp\",\"message\":\""
             << jsonEscape(errorMessage) << "\"}]";
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
                std::cout << fs::current_path().string() << std::endl;
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
                    std::cerr << "chay: thieu duong dan tep\n";
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], false, false);
            }
            if (command == "--disassemble" || command == "--giải mã" || command == "--giải-mã") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << "--giai-ma can duong dan tep\n";
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], true, false);
            }
            if (command == "--lint") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << "--lint can duong dan tep\n";
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], false, true);
            }
            if (command == "--format" || command == "--định dạng" || command == "--định-dạng") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << "--dinh-dang can duong dan tep\n";
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
            if (command == "pkg") {
                return runPackageCommand(argc, argv);
            }
            if (command == "init" || command == "khởi tạo") {
                std::string name = (argc >= (3 + commandWordOffset)) ? argv[2 + commandWordOffset] : "";
                return pkgInit(name);
            }
            if (command == "install" || command == "cai" || command == "caidat" || command == "cài đặt") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << "caidat: thieu duong dan nguon\n";
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
                    std::cerr << "xoa: thieu ten goi\n";
                    return EXIT_FAILURE;
                }
                return pkgRemove(argv[2 + commandWordOffset]);
            }
            if (command == "info" || command == "thông tin") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << "thong tin: thieu ten goi\n";
                    return EXIT_FAILURE;
                }
                return pkgInfo(argv[2 + commandWordOffset]);
            }
            if (command == "has" || command == "kiểm tra") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << "kiem tra: thieu ten goi\n";
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
            return runFile(argv[1], false, false);
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
            if (!fs::path(defaultFile).parent_path().empty()) {
                fs::current_path(fs::path(defaultFile).parent_path());
            }

            vietvm::compiler::StringPool::clear();
            // Reset imported files tracking between compilations
            vietvm::compiler::clearImportedFiles();
            vietvm::compiler::hamMap::hamBytecodeMap.clear();
            vietvm::compiler::hamMap::clearHamNameIndexMap();
            vietvm::compiler::hamMap::resetHamIdCounter();

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
                    const std::string filename = entry.path().string();
                    std::cout << "\n🔹 Đang chạy test: " << filename << std::endl;

                    std::string source = readFile(filename);
                    // Ensure imports inside each test file resolve relative to the test file location
                    // Use RAII-style guard to always restore cwd even on exception
                    CwdGuard cwdGuard(fs::current_path());
                    if (!fs::path(filename).parent_path().empty()) {
                        fs::current_path(fs::path(filename).parent_path());
                    }

                    vietvm::compiler::StringPool::clear();
                    // Reset imported files tracking giữa các lần biên dịch
                    vietvm::compiler::clearImportedFiles();
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
            std::cerr << "Thư mục tests/ không tồn tại.\n";
        }

    } catch (const std::exception &ex) {
        std::cerr << "Lỗi: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
