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
#include "vm/vm.h"
#include "frontend/keywords.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/tooling/tooling.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/project_layout.h"
#include "vpp/core/text.h"

namespace fs = std::filesystem;
namespace messages = vietvm::messages;

// Giữ lại thư mục làm việc hiện tại theo RAII; constructor chụp `current_path()` và destructor tự khôi phục khi rời scope để lệnh CLI tạm đổi thư mục không làm ảnh hưởng caller.
class RuntimeCwdGuard {
public:
    // Ghi nhớ thư mục làm việc tại thời điểm guard được tạo; giá trị này sẽ được dùng để khôi phục ở destructor.
    RuntimeCwdGuard() : saved_(fs::current_path()) {}
    // Khôi phục thư mục làm việc đã lưu khi guard hết vòng đời; lỗi phục hồi được nuốt để destructor không ném exception trong quá trình unwind.
    ~RuntimeCwdGuard() {
        try {
            fs::current_path(saved_);
        } catch (...) {
        }
    }

    // Cấm sao chép guard để hai object không cùng cố khôi phục một trạng thái thư mục đã chụp ở thời điểm khác nhau.
    RuntimeCwdGuard(const RuntimeCwdGuard &) = delete;
    // Cấm phép gán để ownership của trạng thái thư mục đã lưu luôn gắn với đúng một guard.
    RuntimeCwdGuard &operator=(const RuntimeCwdGuard &) = delete;

private:
    fs::path saved_;
};

// In lỗi thông báo; hàm chuyển dữ liệu thành chuỗi và gửi tới luồng đầu ra theo định dạng quy định.
static void printErrorMessage(std::string_view fallback,
                              const std::string &detail) {
    std::cerr << messages::formatMessage(fallback, {detail}) << std::endl;
}

// Nối output sink của VM với stdout/collector của CLI; mọi opcode `in` sau đó đi qua callback này thay vì ghi trực tiếp trong VM.
static void connectVmOutput(VM &vm) {
    vm.setOutputSink([](const std::string &text) { std::cout << text; });
}

// Đọc tệp; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
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

// In usage; hàm chuyển dữ liệu thành chuỗi và gửi tới luồng đầu ra theo định dạng quy định.
static void printUsage() {
    std::cout << messages::messageText(messages::kCliUsage);
}

// In version; hàm chuyển dữ liệu thành chuỗi và gửi tới luồng đầu ra theo định dạng quy định.
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

// Chạy snippet; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runSnippet(const std::string &source,
                      const fs::path &resolutionBase,
                      SnippetMode mode) {
    const bool emitMainCall = mode == SnippetMode::Execute ||
                              mode == SnippetMode::Disassemble;
    vietvm::compiler::CompilationContext compilationContext;
    compilationContext.importResolutionBase = resolutionBase;
    vietvm::compiler::CompilationArtifacts artifacts =
        vietvm::compiler::compilePipeline(
            compilationContext, source, keywordMap, emitMainCall);
    const auto &stringPool = compilationContext.stringPool;

    switch (mode) {
        case SnippetMode::DumpAst:
            std::cout << vietvm::tooling::dumpAst(artifacts.ast);
            return EXIT_SUCCESS;
        case SnippetMode::DumpIr:
            // compilePipeline returns the post-optimizer IR consumed by the
            // direct emitter. Unsupported regions are rejected instead of
            // falling back to a token backend.
            std::cout << "backend=direct-ir codegen-unsupported-regions="
                      << artifacts.unsupportedDirectIrRegions << '\n';
            std::cout << vietvm::tooling::dumpIr(artifacts.ir);
            return EXIT_SUCCESS;
        case SnippetMode::Disassemble:
            std::cout << vietvm::tooling::disassembleBytecode(artifacts.bytecode, stringPool);
            return EXIT_SUCCESS;
        case SnippetMode::Execute:
            break;
    }

    VM vm(artifacts.bytecode, stringPool);
    connectVmOutput(vm);
    vm.hamBytecodeMap = compilationContext.functionBytecode;
    for (const auto &entry : compilationContext.functionNameIndices) {
        vm.functionTableByNameIndex[entry.second] = entry.first;
    }
    for (const auto &module : compilationContext.moduleInitializers) {
        (void)vm.addModuleInitializer(module.identity, module.bytecode);
    }

    // Relative runtime file/database paths historically resolve beside the
    // entry source file. Import resolution above no longer depends on process
    // cwd, so keep this compatibility scope limited to VM execution.
    RuntimeCwdGuard runtimeCwdGuard;
    if (!resolutionBase.empty()) {
        fs::current_path(resolutionBase);
    }
    vm.run();
    return EXIT_SUCCESS;
}

// Chạy tệp; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
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

// Chạy repl; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
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

// Tạo đường dẫn tới manifest của package từ thư mục package; hàm ghép root với tên file manifest theo layout chuẩn.
static fs::path packageManifestPath(const fs::path &root) {
    return root / vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);
}

// Xác định thư mục gốc package từ working directory hiện tại; hàm chuẩn hóa path trước khi các lệnh `pkg` đọc/ghi metadata.
static fs::path packageRootPath(const fs::path &root) {
    for (const char *directoryName : vietvm::core::kPackageDirectoryNames) {
        const fs::path candidate = root / vietvm::core::utf8Path(directoryName);
        if (fs::exists(candidate)) return candidate;
    }
    return root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory);
}

// Đọc danh sách package đã khai báo trong manifest/config; hàm parse từng entry và trả danh sách đã chuẩn hóa cho lệnh CLI.
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

// Ghi gói manifest; hàm tuần tự hóa hoặc chuyển dữ liệu đầu vào sang đích ghi tương ứng.
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

// Khởi tạo package V++ mới; lệnh tạo cấu trúc thư mục/manifest mặc định sau khi kiểm tra đích chưa xung đột.
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

// Khởi tạo backend/config dự án theo tùy chọn CLI; hàm tạo các file nền cần thiết để compiler/runtime nhận diện dự án.
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

// Thêm dependency/package vào manifest; hàm kiểm tra trùng, cập nhật metadata rồi ghi lại file cấu hình.
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

// In các package/dependency đang khai báo; hàm đọc manifest và định dạng từng entry cho CLI.
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

// Xóa package khỏi manifest; hàm tìm entry theo tên, loại bỏ rồi ghi lại cấu hình khi có thay đổi.
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

// Kiểm tra package đã tồn tại trong manifest hay chưa; hàm duyệt danh sách package và so tên chuẩn hóa.
static bool packageExists(const fs::path &root, const std::string &packageName) {
    if (packageName.empty()) return false;
    fs::path packageMain = vietvm::core::packageEntryPath(
        packageRootPath(root) / vietvm::core::utf8Path(packageName));
    return fs::exists(packageMain);
}

// In thông tin chi tiết của một package; hàm tra manifest theo tên rồi hiển thị metadata và trạng thái liên quan.
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

// Trả kết quả CLI cho việc package có tồn tại hay không; hàm dùng `packageExists` và chuyển boolean thành exit/output phù hợp.
static int pkgHas(const std::string &packageName) {
    bool exists = packageExists(fs::current_path(), packageName);
    std::cout << messages::messageText(exists ? messages::kPkgValuePresent
                                               : messages::kPkgValueAbsent) << std::endl;
    return exists ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Tính và in thống kê package hiện tại; hàm tổng hợp số package cùng metadata cần thiết từ manifest.
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

// Chạy gói lệnh; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
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

// Chạy doctor; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
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

// Escape chuỗi để nhúng an toàn vào JSON của LSP/CLI; hàm thay dấu nháy, backslash và ký tự điều khiển bằng escape sequence.
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

// Giải escape từ chuỗi JSON đơn giản; hàm đọc backslash sequence và khôi phục ký tự gốc cho parser LSP.
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

// Đọc LSP thông báo; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
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

// Trích xuất JSON chuỗi trường; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
static std::string extractJsonStringField(const std::string &body, const std::string &field) {
    std::regex rx("\"" + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
    std::smatch match;
    if (std::regex_search(body, match, rx) && match.size() > 1) {
        return jsonUnescape(match[1].str());
    }
    return "";
}

// Trích xuất JSON thô trường; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
static std::string extractJsonRawField(const std::string &body, const std::string &field) {
    std::regex rx("\"" + field + "\"\\s*:\\s*([^,}]+)");
    std::smatch match;
    if (std::regex_search(body, match, rx) && match.size() > 1) {
        return vietvm::core::trim(match[1].str());
    }
    return "";
}

// Ghi LSP thông báo; hàm tuần tự hóa hoặc chuyển dữ liệu đầu vào sang đích ghi tương ứng.
static void writeLspMessage(const std::string &payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush;
}

// Gửi `textDocument/publishDiagnostics` qua LSP; hàm chuyển diagnostic compiler thành JSON-RPC notification kèm range/message.
static void publishDiagnostics(const std::string &uri, const std::string &text) {
    std::string errorMessage;
    std::string result = "[]";
    if (!vietvm::tooling::lintSource(text, errorMessage)) {
        std::ostringstream diag;
        diag << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
             << "\"severity\":1,\"source\":\"vpp\"";
        diag << ",\"message\":\"" << jsonEscape(errorMessage) << "\"}]";
        result = diag.str();
    }

    std::ostringstream notif;
    notif << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
          << "\"uri\":\"" << jsonEscape(uri) << "\","
          << "\"diagnostics\":" << result << "}}";
    writeLspMessage(notif.str());
}

// Chạy language máy chủ; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
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


// Điểm vào chính của chương trình.
int main(int argc, char* argv[]) {
    try {
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
            const fs::path defaultPath = vietvm::core::utf8Path(defaultFile);
            return runSnippet(
                source,
                defaultPath.parent_path().empty()
                    ? fs::current_path()
                    : defaultPath.parent_path(),
                SnippetMode::Execute);
        }

        std::string testDir = "../../src/tests";
        if (fs::exists(testDir)) {
            for (const auto& entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".vi") {
                    const std::string filename = entry.path().u8string();
                    std::cout << messages::messageText(messages::kCliTestRunning, {filename})
                              << std::endl;

                    std::string source = readFile(filename);
                    const fs::path testPath = vietvm::core::utf8Path(filename);
                    (void)runSnippet(
                        source,
                        testPath.parent_path().empty()
                            ? fs::current_path()
                            : testPath.parent_path(),
                        SnippetMode::Execute);
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
