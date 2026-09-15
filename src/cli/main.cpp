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
#include "vpp/runtime/error.h"
#include "vpp/tooling/tooling.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/package_cache.h"
#include "vpp/core/package_installer.h"
#include "vpp/core/package_manifest.h"
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
    return vietvm::core::resolvePackageDependencyGraph(manifest, root);
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

        vietvm::core::PackageLockEntry entry;
        entry.name = dependency.name;
        entry.version = dependency.version;
        entry.sourceKind = dependency.sourceKind;
        entry.location = relativePackageLocation(dependency.sourcePath, root);
        try {
            entry.resolvedPath = fs::relative(installed, root).generic_u8string();
        } catch (...) {
            entry.resolvedPath = installed.lexically_normal().generic_u8string();
        }
        entry.fingerprint = vietvm::core::fingerprintPackageTree(installed);
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
static int pkgAdd(const std::string &sourceArg, const std::string &packageNameArg) {
    fs::path sourcePath = vietvm::core::utf8Path(sourceArg);
    if (!fs::exists(sourcePath)) {
        std::cerr << messages::formatMessage(messages::kPkgSourceNotFound, {sourceArg}) << std::endl;
        return EXIT_FAILURE;
    }

    std::optional<vietvm::core::ProjectManifest> sourceProject;
    if (fs::is_directory(sourcePath)) {
        const fs::path sourceManifest = packageManifestPath(sourcePath);
        if (fs::exists(sourceManifest)) {
            sourceProject = vietvm::core::readProjectManifest(sourceManifest);
        }
    }

    std::string packageName = packageNameArg;
    if (packageName.empty()) {
        if (sourceProject.has_value() && !sourceProject->name.empty()) {
            packageName = sourceProject->name;
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
    updated.versionRange = "*";
    updated.sourceKind = vietvm::core::PackageSourceKind::Path;
    updated.location = sourceArg;
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
    materializeResolvedPackages(projectRoot, graph);
    vietvm::core::writeProjectManifest(packageManifestPath(projectRoot), manifest);
    if (writeResolvedPackageLock(projectRoot, graph, false) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    std::cout << messages::messageText(messages::kPkgInstalled, {packageName});
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

        if (entry.sourceKind != vietvm::core::PackageSourceKind::Path) {
            throw std::runtime_error(
                "phục hồi: source '" +
                vietvm::core::packageSourceKindName(entry.sourceKind) +
                "' chưa được hỗ trợ cho package '" + entry.name + "'");
        }

        fs::path sourcePath = vietvm::core::utf8Path(entry.location);
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
    if (sub == "install" || sub == "cài đặt") {
        if (argc < (4 + subWordOffset)) {
            return pkgRestore(false);
        }
        std::string sourceArg = argv[3 + subWordOffset];
        if (isOfflineFlag(sourceArg)) return pkgRestore(true);
        std::string name = (argc >= (5 + subWordOffset)) ? argv[4 + subWordOffset] : "";
        return pkgAdd(sourceArg, name);
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
                if (argc < (3 + commandWordOffset)) return pkgRestore(false);
                std::string sourceArg = argv[2 + commandWordOffset];
                if (isOfflineFlag(sourceArg)) return pkgRestore(true);
                std::string name = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                return pkgAdd(sourceArg, name);
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
