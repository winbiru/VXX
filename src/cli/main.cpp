#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>
#include "vm/vm.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/runtime/error.h"
#include "vpp/tooling/tooling.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/package_cache.h"
#include "vpp/core/package_installer.h"
#include "vpp/core/package_manifest.h"
#include "vpp/core/package_source.h"
#include "vpp/core/package_solver.h"
#include "vpp/core/project_layout.h"
#include "vpp/core/semver.h"
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

static void verifyLockedProjectForSource(const fs::path &sourcePath);

// Chạy snippet; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runSnippet(const std::string &source,
                      const fs::path &resolutionBase,
                      SnippetMode mode,
                      const fs::path &sourceIdentity = {}) {
    const bool emitMainCall = mode == SnippetMode::Execute ||
                              mode == SnippetMode::Disassemble;
    vietvm::compiler::CompilationContext compilationContext;
    compilationContext.importResolutionBase = resolutionBase;
    if (!sourceIdentity.empty()) {
        compilationContext.currentSourceIdentity =
            sourceIdentity.lexically_normal().u8string();
    } else {
        compilationContext.currentSourceIdentity = "<memory>";
    }
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
    vm.setDebugInfo(artifacts.bytecodeDebugInfo,
                    compilationContext.functionDebugInfo);
    for (const auto &entry : compilationContext.functionNameIndices) {
        vm.functionTableByNameIndex[entry.second] = entry.first;
    }
    for (const auto &module : compilationContext.moduleInitializers) {
        (void)vm.addModuleInitializer(module.identity, module.bytecode,
                                      module.debugInfo);
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

    verifyLockedProjectForSource(filePath);

    return runSnippet(source,
                      fileDir.empty() ? fs::current_path() : fileDir,
                      mode,
                      filePath);
}

// Biên dịch một tệp V++ mà không chạy VM. Lệnh `dựng` dùng contract này trong
// giai đoạn bytecode 1.0 chưa đóng format artifact trên đĩa: compile pipeline
// phải thành công và metadata/function registry phải hoàn chỉnh trong bộ nhớ.
static int buildFile(const std::string &filename) {
    const std::string source = readFile(filename);
    const fs::path filePath = vietvm::core::utf8Path(filename);
    const fs::path fileDir = filePath.parent_path();
    verifyLockedProjectForSource(filePath);

    vietvm::compiler::CompilationContext context;
    context.importResolutionBase =
        fileDir.empty() ? fs::current_path() : fileDir;
    context.currentSourceIdentity = filePath.lexically_normal().u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, source, keywordMap, true);

    std::cout << messages::messageText(
        messages::kCliBuildSucceeded,
        {filename,
         std::to_string(artifacts.bytecode.size()),
         std::to_string(context.functionBytecode.size())});
    return EXIT_SUCCESS;
}

// Thu thập toàn bộ tệp `.vi` dưới một tệp/thư mục theo thứ tự xác định để các
// lệnh kiểm thử, soát lỗi và định dạng có cùng quy tắc discovery.
static std::vector<fs::path> collectSourceFiles(const fs::path &path) {
    std::vector<fs::path> sourceFiles;
    if (fs::is_regular_file(path)) {
        if (path.extension() == ".vi") sourceFiles.push_back(path);
    } else if (fs::is_directory(path)) {
        for (const auto &entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".vi") {
                sourceFiles.push_back(entry.path());
            }
        }
    }
    std::sort(sourceFiles.begin(), sourceFiles.end());
    return sourceFiles;
}

// Chạy toàn bộ tệp `.vi` trong một đường dẫn kiểm thử theo thứ tự xác định.
// Nếu đầu vào là một tệp thì chỉ chạy tệp đó; nếu là thư mục thì quét đệ quy.
static int runTests(const fs::path &requestedPath) {
    fs::path path = requestedPath;
    if (path.empty()) path = vietvm::core::utf8Path("tests");
    if (!fs::exists(path)) {
        std::cerr << messages::formatMessage(
            messages::kCliTestPathMissing, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    const std::vector<fs::path> testFiles = collectSourceFiles(path);

    if (testFiles.empty()) {
        std::cerr << messages::formatMessage(
            messages::kCliTestNoFiles, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t passed = 0;
    for (const fs::path &testFile : testFiles) {
        std::cout << messages::messageText(
            messages::kCliTestRunning, {testFile.u8string()}) << std::endl;
        try {
            if (runFile(testFile.u8string(), SnippetMode::Execute) == EXIT_SUCCESS) {
                ++passed;
            }
        } catch (const vietvm::runtime::RuntimeError &error) {
            printErrorMessage(messages::kCliUnhandledException,
                              vietvm::runtime::formatRuntimeError(error));
        } catch (const std::exception &error) {
            printErrorMessage(messages::kCliUnhandledException, error.what());
        }
    }

    std::cout << messages::messageText(
        messages::kCliTestSummary,
        {std::to_string(passed), std::to_string(testFiles.size())});
    return passed == testFiles.size() ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int runLintPath(const fs::path &path) {
    if (!fs::exists(path)) {
        std::cerr << messages::formatMessage(
            messages::kCliToolPathMissing, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }
    const auto sourceFiles = collectSourceFiles(path);
    if (sourceFiles.empty()) {
        std::cerr << messages::formatMessage(
            messages::kCliToolNoSourceFiles, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const fs::path &sourceFile : sourceFiles) {
        const std::string source = readFile(sourceFile.u8string());
        const fs::path resolutionBase = sourceFile.parent_path().empty()
            ? fs::current_path()
            : sourceFile.parent_path();
        const auto diagnostics = vietvm::tooling::lintDiagnostics(source, resolutionBase);
        for (const auto &diagnostic : diagnostics) {
            const bool isError =
                diagnostic.severity == vietvm::tooling::DiagnosticSeverity::Error;
            if (isError) ++errors;
            else ++warnings;
            std::cerr << messages::messageText(
                messages::kCliLintDiagnostic,
                {sourceFile.u8string(),
                 std::to_string(diagnostic.span.begin.line),
                 std::to_string(diagnostic.span.begin.column),
                 messages::messageText(isError ? messages::kToolSeverityError
                                                : messages::kToolSeverityWarning),
                 diagnostic.message});
        }
    }
    std::cout << messages::messageText(
        messages::kCliLintSummary,
        {std::to_string(sourceFiles.size()),
         std::to_string(errors),
         std::to_string(warnings)});
    return errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int runFormatPath(const fs::path &path,
                         bool writeFiles,
                         bool checkOnly) {
    if (writeFiles && checkOnly) {
        std::cerr << messages::formatMessage(messages::kCliFormatConflictingModes) << '\n';
        return EXIT_FAILURE;
    }
    if (!fs::exists(path)) {
        std::cerr << messages::formatMessage(
            messages::kCliToolPathMissing, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }
    const auto sourceFiles = collectSourceFiles(path);
    if (sourceFiles.empty()) {
        std::cerr << messages::formatMessage(
            messages::kCliToolNoSourceFiles, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }
    if (fs::is_directory(path) && !writeFiles && !checkOnly) {
        std::cerr << messages::formatMessage(messages::kCliFormatDirectoryNeedsMode) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t changed = 0;
    for (const fs::path &sourceFile : sourceFiles) {
        const std::string source = readFile(sourceFile.u8string());
        const std::string formatted = vietvm::tooling::formatSource(source);
        if (formatted != source) {
            ++changed;
            if (checkOnly) {
                std::cerr << messages::messageText(
                    messages::kCliFormatChanged, {sourceFile.u8string()});
            } else if (writeFiles) {
                std::ofstream output(sourceFile, std::ios::binary | std::ios::trunc);
                if (!output.is_open()) {
                    std::cerr << messages::formatMessage(
                        messages::kCliFormatWriteFailed, {sourceFile.u8string()}) << '\n';
                    return EXIT_FAILURE;
                }
                output << formatted;
            }
        }
        if (!writeFiles && !checkOnly) std::cout << formatted;
    }

    if (writeFiles || checkOnly) {
        std::cout << messages::messageText(
            messages::kCliFormatSummary,
            {std::to_string(sourceFiles.size()), std::to_string(changed)});
    }
    return checkOnly && changed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
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
        if (trimmed == ":thoát" || trimmed == ":quit" || trimmed == ":exit") break;
        if (trimmed == ":giúp" || trimmed == ":help") {
            std::cout << messages::messageText(messages::kReplHelp);
            continue;
        }

        std::string source = "nhập \"stdlib\";\n" + line;
        try {
            (void)runSnippet(source, fs::current_path(), SnippetMode::Execute,
                             fs::path("<repl>"));
        } catch (const vietvm::runtime::RuntimeError &error) {
            printErrorMessage(messages::kReplExecutionFailed,
                              vietvm::runtime::formatRuntimeError(error));
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

static fs::path packageLockPath(const fs::path &root) {
    return root / vietvm::core::utf8Path(vietvm::core::kPackageLockFile);
}

// Xác định thư mục gốc package từ working directory hiện tại; hàm chuẩn hóa path trước khi các lệnh `pkg` đọc/ghi metadata.
static fs::path packageRootPath(const fs::path &root) {
    for (const char *directoryName : vietvm::core::kPackageDirectoryNames) {
        const fs::path candidate = root / vietvm::core::utf8Path(directoryName);
        if (fs::exists(candidate)) return candidate;
    }
    return root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory);
}

static void verifyLockedProjectForSource(const fs::path &sourcePath) {
    fs::path start = sourcePath.parent_path();
    if (start.empty()) start = fs::current_path();
    try {
        start = fs::absolute(start).lexically_normal();
    } catch (...) {
        start = start.lexically_normal();
    }

    std::optional<fs::path> projectRoot;
    for (fs::path dir = start;; dir = dir.parent_path()) {
        if (fs::exists(packageLockPath(dir))) {
            projectRoot = dir;
            break;
        }
        if (dir == dir.parent_path()) break;
    }
    if (!projectRoot.has_value()) return;

    const auto lockfile =
        vietvm::core::readPackageLockfile(packageLockPath(*projectRoot));
    const fs::path installedRoot = packageRootPath(*projectRoot);
    for (const auto &entry : lockfile.packages) {
        const fs::path installed =
            installedRoot / vietvm::core::utf8Path(entry.name);
        if (!fs::exists(installed) || !fs::is_directory(installed)) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPkgLockedPackageMissing,
                {entry.name, installed.u8string()}));
        }
        const std::string actual =
            vietvm::core::fingerprintPackageTree(installed);
        if (actual != entry.fingerprint) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPkgLockedPackageChanged,
                {entry.name, entry.fingerprint, actual}));
        }
    }
}

// Quét package đã có trên đĩa để tương thích project cũ chưa có manifest schema 1.
static std::vector<std::string> scanInstalledPackages(const fs::path &root) {
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

// Tạo manifest mặc định từ project root; package có sẵn được ghi thành path
// dependency để `vpp gói khởi tạo` có thể nâng project legacy mà không mất state.
static vietvm::core::ProjectManifest createProjectManifest(
    const fs::path &root,
    const std::string &name) {
    vietvm::core::ProjectManifest manifest;
    manifest.name = name.empty() ? root.filename().u8string() : name;
    manifest.version = vietvm::core::kCliVersion;
    for (const std::string &packageName : scanInstalledPackages(root)) {
        vietvm::core::PackageDependencySpec dependency;
        dependency.name = packageName;
        dependency.versionRange = "*";
        dependency.sourceKind = vietvm::core::PackageSourceKind::Path;
        dependency.location =
            (vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
             vietvm::core::utf8Path(packageName)).generic_u8string();
        manifest.dependencies.push_back(std::move(dependency));
    }
    return manifest;
}

static vietvm::core::ProjectManifest loadOrCreateProjectManifest(
    const fs::path &root) {
    const fs::path manifestPath = packageManifestPath(root);
    if (fs::exists(manifestPath)) {
        return vietvm::core::readProjectManifest(manifestPath);
    }
    return createProjectManifest(root, root.filename().u8string());
}

// Đọc dependency theo manifest. Project chưa có manifest vẫn dùng scan legacy
// để các lệnh list/doctor không đổi hành vi trước khi người dùng chạy init.
static std::vector<std::string> listPackages(const fs::path &root) {
    const fs::path manifestPath = packageManifestPath(root);
    if (!fs::exists(manifestPath)) return scanInstalledPackages(root);

    const auto manifest = vietvm::core::readProjectManifest(manifestPath);
    std::vector<std::string> packages;
    packages.reserve(manifest.dependencies.size());
    for (const auto &dependency : manifest.dependencies) {
        packages.push_back(dependency.name);
    }
    std::sort(packages.begin(), packages.end());
    return packages;
}

static std::string relativePackageLocation(const fs::path &path,
                                           const fs::path &projectRoot) {
    try {
        const fs::path relative = fs::relative(path, projectRoot);
        if (!relative.empty()) return relative.generic_u8string();
    } catch (...) {
    }
    return path.lexically_normal().generic_u8string();
}

static std::string packageSourceVersion(const fs::path &sourcePath) {
    if (fs::is_directory(sourcePath)) {
        const fs::path manifestPath = packageManifestPath(sourcePath);
        if (fs::exists(manifestPath)) {
            return vietvm::core::readProjectManifest(manifestPath).version;
        }
    }
    return "0.0.0+local";
}

static vietvm::core::ResolvedPackageGraph resolveProjectPackages(
    const fs::path &root,
    const vietvm::core::ProjectManifest &manifest) {
    const fs::path sourceStateRoot =
        root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
        "sources";
    return vietvm::core::resolvePackageDependencyGraph(
        manifest, root,
        [sourceStateRoot](const vietvm::core::PackageDependencySpec &dependency,
                          const fs::path &declaringRoot) {
            return vietvm::core::materializePackageSource(
                dependency, declaringRoot, sourceStateRoot);
        });
}

static void materializeResolvedPackages(
    const fs::path &root,
    const vietvm::core::ResolvedPackageGraph &graph) {
    const fs::path packageRoot = packageRootPath(root);
    const fs::path cacheRoot = vietvm::core::packageCacheRoot(root);
    fs::create_directories(packageRoot);
    for (const auto &package : graph.packages) {
        const fs::path target =
            packageRoot / vietvm::core::utf8Path(package.name);
        const std::string fingerprint =
            vietvm::core::materializePathPackage(package.sourcePath, target);
        (void)vietvm::core::cachePackageTree(target, cacheRoot, fingerprint);
    }
}

static int writeResolvedPackageLock(
    const fs::path &root,
    const vietvm::core::ResolvedPackageGraph &graph,
    bool announce) {
    vietvm::core::PackageLockfile lockfile;
    const fs::path packageRoot = packageRootPath(root);
    const fs::path cacheRoot = vietvm::core::packageCacheRoot(root);

    for (const auto &dependency : graph.packages) {
        const fs::path installed =
            packageRoot / vietvm::core::utf8Path(dependency.name);
        if (!fs::exists(installed) || !fs::is_directory(installed)) {
            std::cerr << messages::formatMessage(
                messages::kPkgLockDependencyMissing, {dependency.name}) << '\n';
            return EXIT_FAILURE;
        }

        const std::string installedVersion = packageSourceVersion(installed);
        if (installedVersion != dependency.version) {
            std::cerr << messages::formatMessage(
                messages::kPkgLockInstalledVersionMismatch,
                {dependency.name, installedVersion, dependency.version}) << '\n';
            return EXIT_FAILURE;
        }

        // A source can move without changing its package version (notably a Git
        // branch/tag). Locking the newly resolved source identity together with
        // stale vendored bytes would produce an impossible lock entry. Reuse the
        // real installer staging/filtering path to verify that the installed
        // artifact is exactly what the current resolved source materializes to.
        const std::string installedFingerprint =
            vietvm::core::fingerprintPackageTree(installed);
        const fs::path verificationTarget =
            root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
            "lock-verify" / vietvm::core::utf8Path(dependency.name);
        std::error_code cleanupError;
        fs::remove_all(verificationTarget, cleanupError);
        try {
            (void)vietvm::core::materializePathPackage(
                dependency.sourcePath, verificationTarget, installedFingerprint);
        } catch (const std::exception &error) {
            fs::remove_all(verificationTarget, cleanupError);
            throw std::runtime_error(
                "không thể khóa package '" + dependency.name +
                "': bytes đã cài không khớp source vừa resolve; hãy chạy 'vpp cập nhật' "
                "hoặc 'vpp đồng bộ' trước. Chi tiết: " + error.what());
        }
        fs::remove_all(verificationTarget, cleanupError);

        vietvm::core::PackageLockEntry entry;
        entry.name = dependency.name;
        entry.version = dependency.version;
        entry.sourceKind = dependency.sourceKind;
        entry.location =
            (dependency.sourceKind == vietvm::core::PackageSourceKind::Git ||
             dependency.sourceKind == vietvm::core::PackageSourceKind::Registry)
                ? dependency.resolvedLocation
                : relativePackageLocation(dependency.sourcePath, root);
        try {
            entry.resolvedPath = fs::relative(installed, root).generic_u8string();
        } catch (...) {
            entry.resolvedPath = installed.lexically_normal().generic_u8string();
        }
        entry.fingerprint = installedFingerprint;
        entry.revision = dependency.sourceRevision;
        (void)vietvm::core::cachePackageTree(
            installed, cacheRoot, entry.fingerprint);
        lockfile.packages.push_back(std::move(entry));
    }

    const fs::path lockPath = packageLockPath(root);
    vietvm::core::writePackageLockfile(lockPath, lockfile);
    if (announce) {
        std::cout << messages::messageText(messages::kPkgLocked, {lockPath.u8string()});
    }
    return EXIT_SUCCESS;
}

// Ghi manifest schema 1 theo model package chung thay vì tự ghép JSON trong CLI.
static void writePackageManifest(const fs::path &root, const std::string &name) {
    const auto manifest = createProjectManifest(root, name);
    vietvm::core::writeProjectManifest(packageManifestPath(root), manifest);
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

// Tìm thư mục template theo VPP_HOME trước, sau đó đi ngược từ cwd để hỗ trợ
// cả release đã cài và chạy trực tiếp trong source tree.
static fs::path findProjectTemplate(const std::string &templateName) {
    if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
        const fs::path candidate =
            fs::u8path(vppHome) / "templates" / vietvm::core::utf8Path(templateName);
        if (fs::exists(candidate) && fs::is_directory(candidate)) return candidate;
    }
    for (fs::path dir = fs::current_path(); ; dir = dir.parent_path()) {
        const fs::path candidate =
            dir / "templates" / vietvm::core::utf8Path(templateName);
        if (fs::exists(candidate) && fs::is_directory(candidate)) return candidate;
        if (dir == dir.parent_path()) break;
    }
    return {};
}

static bool copyProjectTemplate(const fs::path &templateRoot,
                                const fs::path &projectRoot,
                                std::string &failedPath,
                                std::string &errorMessage) {
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(templateRoot, ec)) {
        if (ec) {
            failedPath = templateRoot.u8string();
            errorMessage = ec.message();
            return false;
        }
        const fs::path relative = fs::relative(entry.path(), templateRoot, ec);
        if (ec) {
            failedPath = entry.path().u8string();
            errorMessage = ec.message();
            return false;
        }
        const fs::path target = projectRoot / relative;
        if (entry.is_directory()) {
            fs::create_directories(target, ec);
        } else if (entry.is_regular_file()) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) fs::copy_file(entry.path(), target, fs::copy_options::none, ec);
        }
        if (ec) {
            failedPath = relative.generic_u8string();
            errorMessage = ec.message();
            return false;
        }
    }
    return true;
}

// Khởi tạo application project theo layout 1.0: manifest ở root, source trong
// `src/`, test trong `tests/` và `gói/` dành cho dependency đã materialize.
static int applicationInit(const std::string &name) {
    if (name.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgApplicationNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    const fs::path root = fs::current_path() / vietvm::core::utf8Path(name);
    if (fs::exists(root)) {
        std::cerr << messages::formatMessage(messages::kPkgApplicationDirectoryExists,
                                             {root.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    const fs::path templateRoot = findProjectTemplate("application");
    if (templateRoot.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgApplicationTemplatesMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    writePackageManifest(root, name);
    std::string failedPath;
    std::string errorMessage;
    if (!copyProjectTemplate(templateRoot, root, failedPath, errorMessage)) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cerr << messages::formatMessage(messages::kPkgApplicationTemplateCopyFailed,
                                             {failedPath, errorMessage}) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << messages::messageText(messages::kPkgApplicationCreated, {root.u8string()});
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

    const fs::path templateRoot = findProjectTemplate("backend");
    if (templateRoot.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgBackendTemplatesMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    writePackageManifest(root, name);
    for (const char *filename : {
             "application.vi",
             "config.vi",
             "controller.vi",
             "router.vi",
             "server.vi",
             "application.properties",
             "README.md",
             ".gitignore",
         }) {
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
struct ParsedPackageSourceArgument {
    vietvm::core::PackageSourceKind kind = vietvm::core::PackageSourceKind::Path;
    std::string location;
    std::string reference;
    std::string packageName;
    std::string versionRange = "*";
};

static void parseRegistryPackageSelector(
    const std::string &selector,
    ParsedPackageSourceArgument &parsed) {
    if (selector.empty()) {
        throw std::runtime_error(
            "nguồn registry phải chỉ rõ package theo dạng <tên>[@<range>]");
    }
    const std::size_t at = selector.rfind('@');
    if (at == std::string::npos) {
        parsed.packageName = selector;
        parsed.versionRange = "*";
    } else {
        parsed.packageName = selector.substr(0, at);
        parsed.versionRange = selector.substr(at + 1);
    }
    if (!vietvm::core::isValidPackageName(parsed.packageName) ||
        parsed.versionRange.empty()) {
        throw std::runtime_error("selector registry không hợp lệ: " + selector);
    }
}

static ParsedPackageSourceArgument parsePackageSourceArgument(
    const std::string &sourceArg) {
    ParsedPackageSourceArgument parsed;
    parsed.location = sourceArg;
    if (sourceArg.rfind("registry:", 0) == 0) {
        parsed.kind = vietvm::core::PackageSourceKind::Registry;
        parsed.location.clear();
        parseRegistryPackageSelector(sourceArg.substr(9), parsed);
        return parsed;
    }
    if (sourceArg.rfind("registry+", 0) == 0) {
        parsed.kind = vietvm::core::PackageSourceKind::Registry;
        std::string body = sourceArg.substr(9);
        const std::size_t fragment = body.rfind('#');
        if (fragment == std::string::npos) {
            throw std::runtime_error(
                "nguồn registry phải có dạng registry+<root>#<tên>[@<range>]");
        }
        parsed.location = body.substr(0, fragment);
        if (parsed.location.empty()) {
            throw std::runtime_error("registry root không được rỗng");
        }
        parseRegistryPackageSelector(body.substr(fragment + 1), parsed);
        return parsed;
    }
    if (sourceArg.rfind("git+", 0) != 0) return parsed;

    parsed.kind = vietvm::core::PackageSourceKind::Git;
    parsed.location = sourceArg.substr(4);
    parsed.reference = "HEAD";
    const std::size_t fragment = parsed.location.rfind('#');
    if (fragment != std::string::npos) {
        parsed.reference = parsed.location.substr(fragment + 1);
        parsed.location.resize(fragment);
    }
    if (parsed.location.empty() || parsed.reference.empty()) {
        throw std::runtime_error(
            "nguồn Git phải có dạng git+<repository>[#<ref>]");
    }
    return parsed;
}

static std::string inferGitPackageName(const std::string &location) {
    std::string normalized = location;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();
    const std::size_t slash = normalized.find_last_of('/');
    const std::size_t colon = normalized.find_last_of(':');
    const std::size_t separator =
        slash == std::string::npos ? colon
                                   : (colon == std::string::npos ? slash
                                                                 : std::max(slash, colon));
    std::string name = separator == std::string::npos
                           ? normalized
                           : normalized.substr(separator + 1);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".git") {
        name.resize(name.size() - 4);
    }
    return name;
}

static int pkgAdd(const std::string &sourceArg, const std::string &packageNameArg) {
    const ParsedPackageSourceArgument parsedSource =
        parsePackageSourceArgument(sourceArg);
    fs::path sourcePath;
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Path) {
        sourcePath = vietvm::core::utf8Path(parsedSource.location);
        if (!fs::exists(sourcePath)) {
            std::cerr << messages::formatMessage(
                messages::kPkgSourceNotFound, {parsedSource.location}) << std::endl;
            return EXIT_FAILURE;
        }
    }

    std::optional<vietvm::core::ProjectManifest> sourceProject;
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Path &&
        fs::is_directory(sourcePath)) {
        const fs::path sourceManifest = packageManifestPath(sourcePath);
        if (fs::exists(sourceManifest)) {
            sourceProject = vietvm::core::readProjectManifest(sourceManifest);
        }
    }

    std::string packageName = packageNameArg;
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Registry) {
        if (!packageName.empty() && packageName != parsedSource.packageName) {
            throw std::runtime_error(
                "tên package registry trong source ('" + parsedSource.packageName +
                "') khác tên được truyền ('" + packageName + "')");
        }
        packageName = parsedSource.packageName;
    }
    if (packageName.empty()) {
        if (sourceProject.has_value() && !sourceProject->name.empty()) {
            packageName = sourceProject->name;
        } else if (parsedSource.kind == vietvm::core::PackageSourceKind::Git) {
            packageName = inferGitPackageName(parsedSource.location);
        } else {
            packageName = sourcePath.filename().u8string();
            if (sourcePath.has_extension()) {
                packageName = sourcePath.stem().u8string();
            }
        }
    }

    const fs::path projectRoot = fs::current_path();
    auto manifest = loadOrCreateProjectManifest(projectRoot);
    auto dependency = std::find_if(
        manifest.dependencies.begin(), manifest.dependencies.end(),
        [&](const vietvm::core::PackageDependencySpec &item) {
            return item.name == packageName;
        });
    vietvm::core::PackageDependencySpec updated;
    updated.name = packageName;
    updated.versionRange = parsedSource.versionRange;
    updated.sourceKind = parsedSource.kind;
    updated.location = parsedSource.location;
    updated.reference = parsedSource.reference;
    if (sourceProject.has_value()) {
        updated.versionRange = sourceProject->version;
    }

    if (dependency == manifest.dependencies.end()) {
        manifest.dependencies.push_back(std::move(updated));
    } else {
        *dependency = std::move(updated);
    }

    // Resolve toàn graph trước khi thay package trên đĩa. Version conflict,
    // dependency cycle hoặc source transport chưa hỗ trợ vì vậy thất bại trước
    // bước materialize thay vì để lại dependency graph nửa vời.
    const auto graph = resolveProjectPackages(projectRoot, manifest);
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Git) {
        const auto selected = std::find_if(
            graph.packages.begin(), graph.packages.end(),
            [&](const vietvm::core::ResolvedPackageDependency &item) {
                return item.name == packageName;
            });
        if (selected == graph.packages.end()) {
            throw std::runtime_error(
                "không tìm thấy Git dependency vừa resolve: " + packageName);
        }
        auto direct = std::find_if(
            manifest.dependencies.begin(), manifest.dependencies.end(),
            [&](const vietvm::core::PackageDependencySpec &item) {
                return item.name == packageName;
            });
        if (direct != manifest.dependencies.end()) {
            direct->versionRange = selected->version;
        }
    }
    materializeResolvedPackages(projectRoot, graph);
    vietvm::core::writeProjectManifest(packageManifestPath(projectRoot), manifest);
    if (writeResolvedPackageLock(projectRoot, graph, false) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    std::cout << messages::messageText(messages::kPkgInstalled, {packageName});
    return EXIT_SUCCESS;
}

static int pkgPublish(const std::string &registryArg) {
    if (registryArg.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgPublishRegistryMissing) << '\n';
        return EXIT_FAILURE;
    }
    const auto published = vietvm::core::publishPackageToRegistry(
        fs::current_path(), vietvm::core::utf8Path(registryArg));
    std::cout << messages::messageText(
        messages::kPkgPublished,
        {published.name, published.version, published.path.u8string()});
    return EXIT_SUCCESS;
}

static int pkgSync() {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = packageManifestPath(root);
    if (!fs::exists(manifestPath)) {
        std::cerr << messages::formatMessage(messages::kPkgSyncManifestMissing) << '\n';
        return EXIT_FAILURE;
    }
    const auto manifest = vietvm::core::readProjectManifest(manifestPath);
    const auto graph = resolveProjectPackages(root, manifest);
    materializeResolvedPackages(root, graph);
    if (writeResolvedPackageLock(root, graph, false) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    std::cout << messages::messageText(
        messages::kPkgSynced, {std::to_string(graph.packages.size())});
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
    if (!vietvm::core::isValidPackageName(packageName)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    const fs::path projectRoot = fs::current_path();
    fs::path targetDir = packageRootPath(projectRoot) /
                         vietvm::core::utf8Path(packageName);
    if (!fs::exists(targetDir)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    const fs::path manifestPath = packageManifestPath(projectRoot);
    std::optional<vietvm::core::ProjectManifest> updatedManifest;
    std::optional<vietvm::core::ResolvedPackageGraph> updatedGraph;
    if (fs::exists(manifestPath)) {
        updatedManifest = vietvm::core::readProjectManifest(manifestPath);
        updatedManifest->dependencies.erase(
            std::remove_if(
                updatedManifest->dependencies.begin(), updatedManifest->dependencies.end(),
                [&](const vietvm::core::PackageDependencySpec &dependency) {
                    return dependency.name == packageName;
                }),
            updatedManifest->dependencies.end());

        // Resolve trạng thái sau khi xóa trước khi thay đổi file trên đĩa. Nếu
        // graph còn lại có conflict/source lỗi, lệnh dừng mà package cũ vẫn còn.
        updatedGraph = resolveProjectPackages(projectRoot, *updatedManifest);
    }

    std::error_code ec;
    fs::remove_all(targetDir, ec);
    if (ec) {
        std::cerr << messages::formatMessage(messages::kPkgRemoveFailed,
                                             {packageName, ec.message()}) << '\n';
        return EXIT_FAILURE;
    }

    if (updatedManifest.has_value() && updatedGraph.has_value()) {
        materializeResolvedPackages(projectRoot, *updatedGraph);
        vietvm::core::writeProjectManifest(manifestPath, *updatedManifest);
        if (writeResolvedPackageLock(projectRoot, *updatedGraph, false) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
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

// Khóa toàn dependency graph đã resolve thành exact version/content. Graph dùng
// cùng solver với `đồng bộ`, nên transitive dependency và version conflict được
// xử lý nhất quán giữa install và lock.
static int pkgLock() {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = packageManifestPath(root);
    if (!fs::exists(manifestPath)) {
        std::cerr << messages::formatMessage(messages::kPkgLockManifestMissing) << '\n';
        return EXIT_FAILURE;
    }

    const auto manifest = vietvm::core::readProjectManifest(manifestPath);
    const auto graph = resolveProjectPackages(root, manifest);
    return writeResolvedPackageLock(root, graph, true);
}

static bool isOfflineFlag(const std::string &argument) {
    return argument == "--offline" || argument == "--ngoại-tuyến";
}

static int pkgRestore(bool offlineOnly = false) {
    const fs::path root = fs::current_path();
    const fs::path lockPath = packageLockPath(root);
    if (!fs::exists(lockPath)) {
        std::cerr << messages::formatMessage(messages::kPkgRestoreLockMissing) << '\n';
        return EXIT_FAILURE;
    }

    const auto lockfile = vietvm::core::readPackageLockfile(lockPath);
    const fs::path packageRoot = packageRootPath(root);
    const fs::path cacheRoot = vietvm::core::packageCacheRoot(root);
    fs::create_directories(packageRoot);

    for (const auto &entry : lockfile.packages) {
        const fs::path target =
            packageRoot / vietvm::core::utf8Path(entry.name);

        if (fs::exists(target) && fs::is_directory(target)) {
            const std::string installedFingerprint =
                vietvm::core::fingerprintPackageTree(target);
            if (installedFingerprint == entry.fingerprint) {
                (void)vietvm::core::cachePackageTree(
                    target, cacheRoot, entry.fingerprint);
                continue;
            }
        }

        const auto cached =
            vietvm::core::findCachedPackage(cacheRoot, entry.fingerprint);
        if (cached.has_value()) {
            (void)vietvm::core::materializePathPackage(
                *cached, target, entry.fingerprint);
            continue;
        }

        if (offlineOnly) {
            std::cerr << messages::formatMessage(
                messages::kPkgOfflineCacheMissing,
                {entry.name, entry.fingerprint}) << '\n';
            return EXIT_FAILURE;
        }

        fs::path sourcePath;
        if (entry.sourceKind == vietvm::core::PackageSourceKind::Git) {
            vietvm::core::PackageDependencySpec lockedSource;
            lockedSource.name = entry.name;
            lockedSource.versionRange = entry.version;
            lockedSource.sourceKind = vietvm::core::PackageSourceKind::Git;
            lockedSource.location = entry.location;
            lockedSource.reference = entry.revision;
            const fs::path sourceStateRoot =
                root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
                "sources";
            const auto materialized = vietvm::core::materializePackageSource(
                lockedSource, root, sourceStateRoot);
            if (materialized.revision != entry.revision) {
                throw std::runtime_error(
                    "Git restore resolve sai revision cho package '" + entry.name +
                    "': mong đợi " + entry.revision + ", thực tế " +
                    materialized.revision);
            }
            sourcePath = materialized.path;
        } else if (entry.sourceKind == vietvm::core::PackageSourceKind::Registry) {
            vietvm::core::PackageDependencySpec lockedSource;
            lockedSource.name = entry.name;
            lockedSource.versionRange = entry.version;
            lockedSource.sourceKind = vietvm::core::PackageSourceKind::Registry;
            lockedSource.location = entry.location;
            const fs::path sourceStateRoot =
                root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
                "sources";
            sourcePath = vietvm::core::materializePackageSource(
                lockedSource, root, sourceStateRoot).path;
        } else {
            sourcePath = vietvm::core::utf8Path(entry.location);
            if (!sourcePath.is_absolute()) sourcePath = root / sourcePath;
            try {
                sourcePath = fs::absolute(sourcePath).lexically_normal();
            } catch (...) {
                sourcePath = sourcePath.lexically_normal();
            }
            if (!fs::exists(sourcePath)) {
                std::cerr << messages::formatMessage(
                    messages::kPkgSourceNotFound, {sourcePath.u8string()}) << '\n';
                return EXIT_FAILURE;
            }
        }

        const std::string sourceVersion = packageSourceVersion(sourcePath);
        if (sourceVersion != entry.version) {
            std::cerr << messages::formatMessage(
                messages::kPkgRestoreVersionMismatch,
                {entry.name, sourceVersion, entry.version}) << '\n';
            return EXIT_FAILURE;
        }

        (void)vietvm::core::materializePathPackage(
            sourcePath, target, entry.fingerprint);
        (void)vietvm::core::cachePackageTree(
            target, cacheRoot, entry.fingerprint);
    }

    std::cout << messages::messageText(
        messages::kPkgRestored, {std::to_string(lockfile.packages.size())});
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
            twoWordSub == "cài đặt" || twoWordSub == "phát hành" ||
            twoWordSub == "đồng bộ" || twoWordSub == "cập nhật" ||
            twoWordSub == "phục hồi" || twoWordSub == "thống kê") {
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
    if (sub == "install" || sub == "cài đặt") {
        if (argc < (4 + subWordOffset)) {
            return pkgRestore(false);
        }
        std::string sourceArg = argv[3 + subWordOffset];
        if (isOfflineFlag(sourceArg)) return pkgRestore(true);
        std::string name = (argc >= (5 + subWordOffset)) ? argv[4 + subWordOffset] : "";
        return pkgAdd(sourceArg, name);
    }
    if (sub == "publish" || sub == "phát-hành" || sub == "phát hành") {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgPublishRegistryMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgPublish(argv[3 + subWordOffset]);
    }
    if (sub == "list" || sub == "danh sách") {
        return pkgList();
    }
    if (sub == "sync" || sub == "đồng bộ") {
        return pkgSync();
    }
    if (sub == "update" || sub == "cập nhật") {
        return pkgSync();
    }
    if (sub == "lock" || sub == "khóa") {
        return pkgLock();
    }
    if (sub == "restore" || sub == "phục hồi") {
        const bool offlineOnly =
            argc >= (4 + subWordOffset) &&
            isOfflineFlag(argv[3 + subWordOffset]);
        return pkgRestore(offlineOnly);
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

struct LspPosition {
    std::size_t line = 0;
    std::size_t character = 0;
};

static bool extractLspPosition(const std::string &body, LspPosition &position) {
    const std::regex positionRx(
        "\\\"position\\\"\\s*:\\s*\\{[^}]*\\\"line\\\"\\s*:\\s*([0-9]+)[^}]*"
        "\\\"character\\\"\\s*:\\s*([0-9]+)[^}]*\\}");
    std::smatch match;
    if (!std::regex_search(body, match, positionRx) || match.size() < 3) return false;
    position.line = static_cast<std::size_t>(std::stoull(match[1].str()));
    position.character = static_cast<std::size_t>(std::stoull(match[2].str()));
    return true;
}

static std::size_t utf8SequenceLength(unsigned char lead) noexcept {
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

static std::uint32_t decodeUtf8CodePoint(const std::string &text,
                                         std::size_t offset,
                                         std::size_t &length) noexcept {
    if (offset >= text.size()) {
        length = 0;
        return 0;
    }
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    length = std::min(utf8SequenceLength(lead), text.size() - offset);
    if (length == 1) return lead;
    std::uint32_t value = lead & ((1u << (7u - static_cast<unsigned int>(length))) - 1u);
    for (std::size_t index = 1; index < length; ++index) {
        const unsigned char next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xC0u) != 0x80u) {
            length = 1;
            return lead;
        }
        value = (value << 6u) | (next & 0x3Fu);
    }
    return value;
}

static LspPosition lspPositionForOffset(const std::string &text,
                                        std::size_t targetOffset) noexcept {
    LspPosition position;
    const std::size_t limit = std::min(targetOffset, text.size());
    std::size_t offset = 0;
    while (offset < limit) {
        if (text[offset] == '\n') {
            ++position.line;
            position.character = 0;
            ++offset;
            continue;
        }
        std::size_t length = 1;
        const std::uint32_t codePoint = decodeUtf8CodePoint(text, offset, length);
        position.character += codePoint > 0xFFFFu ? 2u : 1u;
        offset += std::max<std::size_t>(length, 1);
    }
    return position;
}

static std::size_t sourceOffsetForLspPosition(const std::string &text,
                                              LspPosition position) noexcept {
    std::size_t offset = 0;
    std::size_t line = 0;
    while (offset < text.size() && line < position.line) {
        if (text[offset++] == '\n') ++line;
    }
    if (line != position.line) return text.size();

    std::size_t character = 0;
    while (offset < text.size() && text[offset] != '\n' && character < position.character) {
        std::size_t length = 1;
        const std::uint32_t codePoint = decodeUtf8CodePoint(text, offset, length);
        const std::size_t units = codePoint > 0xFFFFu ? 2u : 1u;
        if (character + units > position.character) break;
        character += units;
        offset += std::max<std::size_t>(length, 1);
    }
    return offset;
}

static std::string lspPositionJson(LspPosition position) {
    return "{\"line\":" + std::to_string(position.line) +
           ",\"character\":" + std::to_string(position.character) + "}";
}

static std::string lspRangeJson(const std::string &text,
                                const vietvm::frontend::SourceSpan &span) {
    return "{\"start\":" + lspPositionJson(lspPositionForOffset(text, span.begin.offset)) +
           ",\"end\":" + lspPositionJson(lspPositionForOffset(text, span.end.offset)) + "}";
}

static std::string percentDecode(std::string value) {
    std::string decoded;
    decoded.reserve(value.size());
    auto hexValue = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(value[index]);
    }
    return decoded;
}

static fs::path filePathFromUri(const std::string &uri) {
    constexpr std::string_view prefix = "file://";
    if (uri.rfind(prefix.data(), 0) != 0) return {};
    std::string path = percentDecode(uri.substr(prefix.size()));
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
        path[2] == ':') {
        path.erase(path.begin());
    }
#endif
    return vietvm::core::utf8Path(path);
}

static bool analyzeLspDocument(const std::string &uri,
                               const std::string &text,
                               vietvm::compiler::CompilationContext &context,
                               vietvm::compiler::CompilationArtifacts &artifacts) {
    const fs::path sourcePath = filePathFromUri(uri);
    context.importResolutionBase = sourcePath.empty() || sourcePath.parent_path().empty()
        ? fs::current_path()
        : sourcePath.parent_path();
    context.currentSourceIdentity = sourcePath.empty() ? uri : sourcePath.lexically_normal().u8string();
    try {
        artifacts = vietvm::compiler::compilePipeline(context, text, keywordMap, false);
        return true;
    } catch (...) {
        return false;
    }
}

static bool spanContainsOffset(const vietvm::frontend::SourceSpan &span,
                               std::size_t offset) noexcept {
    return span.begin.offset <= offset && offset < span.end.offset;
}

static const vietvm::frontend::AstStatement *findStatementByTokenBegin(
    const std::vector<vietvm::frontend::AstStatement> &statements,
    std::size_t tokenBegin) {
    for (const auto &statement : statements) {
        if (statement.tokenBegin == tokenBegin) return &statement;
        if (const auto *nested = findStatementByTokenBegin(statement.children, tokenBegin)) {
            return nested;
        }
    }
    return nullptr;
}

static vietvm::frontend::SourceSpan lspDeclarationSpan(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    const vietvm::compiler::SemanticSymbol &symbol) {
    for (const auto &entry : artifacts.semantic.declarationSymbols) {
        if (entry.second != static_cast<int>(symbol.id)) continue;
        const auto *statement = findStatementByTokenBegin(artifacts.ast.statements, entry.first);
        if (statement == nullptr) continue;
        const std::size_t end = std::min(statement->tokenEnd, artifacts.ast.tokens.size());
        for (std::size_t index = statement->tokenBegin; index < end; ++index) {
            if (artifacts.ast.tokens[index].lexeme == symbol.lookupName) {
                return artifacts.ast.tokens[index].span;
            }
        }
    }
    return symbol.declaration;
}

static const vietvm::compiler::SemanticSymbol *symbolAtLspPosition(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    std::size_t offset) {
    const auto &model = artifacts.semantic;
    for (const auto &symbol : model.symbols) {
        if (symbol.origin == vietvm::compiler::SymbolOrigin::Source &&
            spanContainsOffset(lspDeclarationSpan(artifacts, symbol), offset)) {
            return &symbol;
        }
    }

    const vietvm::frontend::AstExpression *bestExpression = nullptr;
    for (const auto &expression : artifacts.ast.expressions) {
        if (!spanContainsOffset(expression.span, offset)) continue;
        if (bestExpression == nullptr ||
            (expression.span.end.offset - expression.span.begin.offset) <
                (bestExpression->span.end.offset - bestExpression->span.begin.offset)) {
            bestExpression = &expression;
        }
    }
    if (bestExpression != nullptr) {
        const auto *binding = model.bindingForExpression(bestExpression->id);
        if (binding != nullptr && binding->kind == vietvm::compiler::BindingKind::Symbol &&
            binding->symbol < model.symbols.size()) {
            return &model.symbols[binding->symbol];
        }
    }

    for (const auto &reference : model.references) {
        if (!spanContainsOffset(reference.span, offset) || reference.resolvedSymbolId < 0) continue;
        const std::size_t symbolId = static_cast<std::size_t>(reference.resolvedSymbolId);
        if (symbolId < model.symbols.size()) return &model.symbols[symbolId];
    }
    return nullptr;
}

static const char *lspSymbolKindName(vietvm::compiler::SemanticSymbolKind kind) noexcept {
    using vietvm::compiler::SemanticSymbolKind;
    switch (kind) {
        case SemanticSymbolKind::Function: return "hàm";
        case SemanticSymbolKind::Method: return "phương thức";
        case SemanticSymbolKind::Class: return "lớp";
        case SemanticSymbolKind::Interface: return "giao diện";
        case SemanticSymbolKind::Parameter: return "tham số";
        case SemanticSymbolKind::GlobalVariable: return "biến toàn cục";
        case SemanticSymbolKind::LocalVariable: return "biến cục bộ";
        case SemanticSymbolKind::ImportAlias: return "bí danh mô-đun";
        case SemanticSymbolKind::CatchVariable: return "biến bắt lỗi";
    }
    return "ký hiệu";
}

static int lspCompletionKind(vietvm::compiler::SemanticSymbolKind kind) noexcept {
    using vietvm::compiler::SemanticSymbolKind;
    switch (kind) {
        case SemanticSymbolKind::Method: return 2;
        case SemanticSymbolKind::Function: return 3;
        case SemanticSymbolKind::Class: return 7;
        case SemanticSymbolKind::Interface: return 8;
        case SemanticSymbolKind::ImportAlias: return 9;
        case SemanticSymbolKind::Parameter:
        case SemanticSymbolKind::GlobalVariable:
        case SemanticSymbolKind::LocalVariable:
        case SemanticSymbolKind::CatchVariable:
            return 6;
    }
    return 1;
}

static vietvm::compiler::ScopeId lspScopeAtOffset(
    const vietvm::compiler::SemanticModel &model,
    std::size_t offset) noexcept {
    vietvm::compiler::ScopeId best = model.globalScope;
    std::size_t bestWidth = std::numeric_limits<std::size_t>::max();
    for (const auto &scope : model.scopes) {
        if (!spanContainsOffset(scope.span, offset)) continue;
        const std::size_t width = scope.span.end.offset - scope.span.begin.offset;
        if (width <= bestWidth) {
            best = scope.id;
            bestWidth = width;
        }
    }
    return best;
}

static bool lspScopeVisibleFrom(const vietvm::compiler::SemanticModel &model,
                                vietvm::compiler::ScopeId declarationScope,
                                vietvm::compiler::ScopeId currentScope) noexcept {
    while (currentScope != vietvm::compiler::kInvalidScopeId && currentScope < model.scopes.size()) {
        if (currentScope == declarationScope) return true;
        currentScope = model.scopes[currentScope].parent;
    }
    return false;
}

static std::string lspCompletionResult(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    std::size_t offset) {
    const auto &model = artifacts.semantic;
    const auto currentScope = lspScopeAtOffset(model, offset);
    std::set<std::string> emitted;
    std::ostringstream result;
    result << "{\"isIncomplete\":false,\"items\":[";
    bool first = true;
    for (const auto &symbol : model.symbols) {
        if (!lspScopeVisibleFrom(model, symbol.declaringScope, currentScope)) continue;
        if (!emitted.insert(symbol.lookupName).second) continue;
        if (!first) result << ',';
        first = false;
        result << "{\"label\":\"" << jsonEscape(symbol.lookupName)
               << "\",\"kind\":" << lspCompletionKind(symbol.kind)
               << ",\"detail\":\"" << jsonEscape(lspSymbolKindName(symbol.kind));
        if ((symbol.kind == vietvm::compiler::SemanticSymbolKind::Function ||
             symbol.kind == vietvm::compiler::SemanticSymbolKind::Method) &&
            symbol.parameterCount > 0) {
            result << " · " << symbol.minimumArgumentCount << ".." << symbol.parameterCount
                   << " tham số";
        }
        result << "\"}";
    }
    result << "]}";
    return result.str();
}

static std::string lspHoverText(const vietvm::compiler::SemanticSymbol &symbol) {
    std::ostringstream text;
    text << lspSymbolKindName(symbol.kind) << ' ' << symbol.lookupName;
    if (symbol.kind == vietvm::compiler::SemanticSymbolKind::Function ||
        symbol.kind == vietvm::compiler::SemanticSymbolKind::Method) {
        text << " — " << symbol.minimumArgumentCount << ".." << symbol.parameterCount
             << " tham số";
    }
    if (symbol.origin == vietvm::compiler::SymbolOrigin::Imported) text << " — đã nhập";
    return text.str();
}

static bool validRenameIdentifier(const std::string &name) {
    if (name.empty()) return false;
    try {
        const auto tokens = vietvm::compiler::postProcessTokensWithSpans(
            vietvm::compiler::tokenizeWithSpans(name));
        return tokens.size() == 1 && tokens.front().lexeme == name &&
               tokens.front().kind == vietvm::frontend::TokenKind::Identifier;
    } catch (...) {
        return false;
    }
}

static std::vector<vietvm::frontend::SourceSpan> lspRenameSpans(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    const vietvm::compiler::SemanticSymbol &symbol) {
    std::vector<vietvm::frontend::SourceSpan> spans;
    std::set<std::pair<std::size_t, std::size_t>> seen;
    auto addSpan = [&](const vietvm::frontend::SourceSpan &span) {
        const auto key = std::make_pair(span.begin.offset, span.end.offset);
        if (seen.insert(key).second) spans.push_back(span);
    };
    addSpan(lspDeclarationSpan(artifacts, symbol));
    for (const auto &expression : artifacts.ast.expressions) {
        const auto *binding = artifacts.semantic.bindingForExpression(expression.id);
        if (binding != nullptr && binding->kind == vietvm::compiler::BindingKind::Symbol &&
            binding->symbol == symbol.id) {
            addSpan(expression.span);
        }
    }
    for (const auto &reference : artifacts.semantic.references) {
        if (reference.resolvedSymbolId == static_cast<int>(symbol.id)) addSpan(reference.span);
    }
    std::sort(spans.begin(), spans.end(), [](const auto &left, const auto &right) {
        return left.begin.offset < right.begin.offset;
    });
    return spans;
}

// Ghi LSP thông báo; hàm tuần tự hóa hoặc chuyển dữ liệu đầu vào sang đích ghi tương ứng.
static void writeLspMessage(const std::string &payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush;
}

// Gửi `textDocument/publishDiagnostics` qua LSP; hàm chuyển diagnostic compiler thành JSON-RPC notification kèm range/message.
static void publishDiagnostics(const std::string &uri, const std::string &text) {
    const fs::path sourcePath = filePathFromUri(uri);
    const fs::path resolutionBase = sourcePath.empty() || sourcePath.parent_path().empty()
        ? fs::current_path()
        : sourcePath.parent_path();
    const auto diagnostics = vietvm::tooling::lintDiagnostics(text, resolutionBase);
    std::ostringstream diagnosticJson;
    diagnosticJson << '[';
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0) diagnosticJson << ',';
        const auto &diagnostic = diagnostics[index];
        const auto &span = diagnostic.span;
        const int severity =
            diagnostic.severity == vietvm::tooling::DiagnosticSeverity::Error ? 1 : 2;
        diagnosticJson
            << "{\"range\":" << lspRangeJson(text, span)
            << ",\"severity\":" << severity
            << ",\"source\":\"vpp\",\"message\":\""
            << jsonEscape(diagnostic.message) << "\"}";
    }
    diagnosticJson << ']';

    std::ostringstream notif;
    notif << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
          << "\"uri\":\"" << jsonEscape(uri) << "\","
          << "\"diagnostics\":" << diagnosticJson.str() << "}}";
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
                     << "\"completionProvider\":{\"triggerCharacters\":[\".\"]},"
                     << "\"definitionProvider\":true,"
                     << "\"hoverProvider\":true,"
                     << "\"renameProvider\":true"
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

        if (method == "textDocument/didClose") {
            const std::string uri = extractJsonStringField(body, "uri");
            if (!uri.empty()) {
                openDocuments.erase(uri);
                std::ostringstream notif;
                notif << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                      << "\"params\":{\"uri\":\"" << jsonEscape(uri)
                      << "\",\"diagnostics\":[]}}";
                writeLspMessage(notif.str());
            }
            continue;
        }

        if (method == "textDocument/completion" ||
            method == "textDocument/definition" ||
            method == "textDocument/hover" ||
            method == "textDocument/rename" ||
            method == "textDocument/formatting") {
            const std::string uri = extractJsonStringField(body, "uri");
            const auto document = openDocuments.find(uri);
            if (document == openDocuments.end()) {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":"
                         << (method == "textDocument/completion" ? "{\"isIncomplete\":false,\"items\":[]}" : "null")
                         << '}';
                writeLspMessage(response.str());
                continue;
            }

            const std::string &text = document->second;
            if (method == "textDocument/formatting") {
                const std::string formatted = vietvm::tooling::formatSource(text);
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":";
                if (formatted == text) {
                    response << "[]";
                } else {
                    const LspPosition end = lspPositionForOffset(text, text.size());
                    response << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":"
                             << lspPositionJson(end) << "},\"newText\":\""
                             << jsonEscape(formatted) << "\"}]";
                }
                response << '}';
                writeLspMessage(response.str());
                continue;
            }

            LspPosition position;
            if (!extractLspPosition(body, position)) {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":null}";
                writeLspMessage(response.str());
                continue;
            }

            vietvm::compiler::CompilationContext context;
            vietvm::compiler::CompilationArtifacts artifacts;
            if (!analyzeLspDocument(uri, text, context, artifacts)) {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":"
                         << (method == "textDocument/completion" ? "{\"isIncomplete\":false,\"items\":[]}" : "null")
                         << '}';
                writeLspMessage(response.str());
                continue;
            }

            const std::size_t offset = sourceOffsetForLspPosition(text, position);
            if (method == "textDocument/completion") {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":" << lspCompletionResult(artifacts, offset) << '}';
                writeLspMessage(response.str());
                continue;
            }

            const auto *symbol = symbolAtLspPosition(artifacts, offset);
            if (method == "textDocument/definition") {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":";
                if (symbol == nullptr || symbol->origin != vietvm::compiler::SymbolOrigin::Source) {
                    response << "null";
                } else {
                    const auto declaration = lspDeclarationSpan(artifacts, *symbol);
                    response << "{\"uri\":\"" << jsonEscape(uri) << "\",\"range\":"
                             << lspRangeJson(text, declaration) << '}';
                }
                response << '}';
                writeLspMessage(response.str());
                continue;
            }

            if (method == "textDocument/hover") {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":";
                if (symbol == nullptr) {
                    response << "null";
                } else {
                    const auto declaration = lspDeclarationSpan(artifacts, *symbol);
                    response << "{\"contents\":{\"kind\":\"plaintext\",\"value\":\""
                             << jsonEscape(lspHoverText(*symbol)) << "\"},\"range\":"
                             << lspRangeJson(text, declaration) << '}';
                }
                response << '}';
                writeLspMessage(response.str());
                continue;
            }

            if (method == "textDocument/rename") {
                const std::string newName = extractJsonStringField(body, "newName");
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id);
                if (symbol == nullptr || symbol->origin != vietvm::compiler::SymbolOrigin::Source ||
                    !validRenameIdentifier(newName)) {
                    response << ",\"result\":null}";
                } else {
                    const auto spans = lspRenameSpans(artifacts, *symbol);
                    response << ",\"result\":{\"changes\":{\"" << jsonEscape(uri) << "\":[";
                    for (std::size_t index = 0; index < spans.size(); ++index) {
                        if (index != 0) response << ',';
                        response << "{\"range\":" << lspRangeJson(text, spans[index])
                                 << ",\"newText\":\"" << jsonEscape(newName) << "\"}";
                    }
                    response << "]}}}";
                }
                writeLspMessage(response.str());
                continue;
            }
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
                    twoWordCommand == "bác sĩ" || twoWordCommand == "chẩn đoán" ||
                    twoWordCommand == "danh sách" ||
                    twoWordCommand == "khởi tạo" || twoWordCommand == "thông tin" ||
                    twoWordCommand == "kiểm tra" || twoWordCommand == "thống kê" ||
                    twoWordCommand == "cài đặt" || twoWordCommand == "kiểm thử" ||
                    twoWordCommand == "phát hành" || twoWordCommand == "đồng bộ" ||
                    twoWordCommand == "cập nhật" || twoWordCommand == "phục hồi" ||
                    twoWordCommand == "--giải mã" ||
                    twoWordCommand == "--định dạng" ||
                    twoWordCommand == "--soát lỗi") {
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
            if (command == "doctor" || command == "bác sĩ" || command == "chẩn đoán") {
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
            if (command == "test" || command == "kiểm thử" || command == "kiểm-thử") {
                const fs::path path =
                    argc >= (3 + commandWordOffset)
                        ? vietvm::core::utf8Path(argv[2 + commandWordOffset])
                        : vietvm::core::utf8Path("tests");
                return runTests(path);
            }
            if (command == "build" || command == "dựng") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliBuildMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return buildFile(argv[2 + commandWordOffset]);
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
            if (command == "--lint" || command == "--soát-lỗi" || command == "--soát lỗi") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliLintMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runLintPath(vietvm::core::utf8Path(argv[2 + commandWordOffset]));
            }
            if (command == "--format" || command == "--định dạng" || command == "--định-dạng") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliFormatMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                const fs::path path = vietvm::core::utf8Path(argv[2 + commandWordOffset]);
                bool writeFiles = false;
                bool checkOnly = false;
                for (int i = 3 + commandWordOffset; i < argc; ++i) {
                    const std::string option = argv[i];
                    if (option == "--in-place" || option == "--ghi-tệp") writeFiles = true;
                    if (option == "--check" || option == "--kiểm-tra") checkOnly = true;
                }
                return runFormatPath(path, writeFiles, checkOnly);
            }
            if (command == "pkg" || command == "gói") {
                return runPackageCommand(argc, argv);
            }
            if (command == "init" || command == "new" || command == "khởi tạo") {
                std::string name = (argc >= (3 + commandWordOffset)) ? argv[2 + commandWordOffset] : "";
                int initSubWordOffset = 0;
                if (name == "ứng" && argc >= (4 + commandWordOffset) &&
                    std::string(argv[3 + commandWordOffset]) == "dụng") {
                    name = "ứng dụng";
                    initSubWordOffset = 1;
                }
                if (name == "ứng dụng" || name == "ứng-dụng" ||
                    name == "app" || name == "application") {
                    std::string applicationName =
                        (argc >= (4 + commandWordOffset + initSubWordOffset))
                            ? argv[3 + commandWordOffset + initSubWordOffset]
                            : "";
                    return applicationInit(applicationName);
                }
                if (name == "backend") {
                    std::string backendName = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                    return backendInit(backendName);
                }
                return pkgInit(name);
            }
            if (command == "install" || command == "cai" || command == "caidat" || command == "cài đặt") {
                if (argc < (3 + commandWordOffset)) return pkgRestore(false);
                std::string sourceArg = argv[2 + commandWordOffset];
                if (isOfflineFlag(sourceArg)) return pkgRestore(true);
                std::string name = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                return pkgAdd(sourceArg, name);
            }
            if (command == "publish" || command == "phát-hành" || command == "phát hành") {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgPublishRegistryMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgPublish(argv[2 + commandWordOffset]);
            }
            if (command == "list" || command == "danh sách") {
                return pkgList();
            }
            if (command == "sync" || command == "đồng bộ") {
                return pkgSync();
            }
            if (command == "update" || command == "cập nhật") {
                return pkgSync();
            }
            if (command == "lock" || command == "khóa") {
                return pkgLock();
            }
            if (command == "restore" || command == "phục hồi") {
                const bool offlineOnly =
                    argc >= (3 + commandWordOffset) &&
                    isOfflineFlag(argv[2 + commandWordOffset]);
                return pkgRestore(offlineOnly);
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
                SnippetMode::Execute,
                defaultPath);
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
                        SnippetMode::Execute,
                        testPath);
                }
            }
        } else {
            std::cerr << messages::formatMessage(messages::kCliTestsDirectoryMissing) << '\n';
        }

    } catch (const vietvm::runtime::RuntimeError &error) {
        printErrorMessage(messages::kCliUnhandledException,
                          vietvm::runtime::formatRuntimeError(error));
        return EXIT_FAILURE;
    } catch (const std::exception &ex) {
        printErrorMessage(messages::kCliUnhandledException, ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
