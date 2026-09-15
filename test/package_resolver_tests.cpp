#include "vpp/compiler/package_resolver.h"
#include "vpp/core/project_layout.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using vietvm::compiler::PackageResolver;

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

class TemporaryTree {
public:
    TemporaryTree() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root_ = fs::temp_directory_path() /
                ("vpp-package-resolver-" + std::to_string(stamp));
        fs::create_directories(root_);
    }

    ~TemporaryTree() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    const fs::path &root() const noexcept { return root_; }

private:
    fs::path root_;
};

void writeFile(const fs::path &path, const std::string &contents = "") {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("cannot create package resolver test file");
    }
    output << contents;
}

fs::path packageMain(const fs::path &root, const std::string &name) {
    return vietvm::core::packageEntryPath(
        root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
        vietvm::core::utf8Path(name));
}

void testExplicitImportWalksUpwardFromCompilationBase() {
    TemporaryTree tree;
    const fs::path resolutionBase = tree.root() / "build" / "deep";
    const fs::path source = tree.root() / "src" / fs::u8path(u8"mô đun.vi");
    fs::create_directories(resolutionBase);
    writeFile(source, "module");

    const PackageResolver resolver(resolutionBase);
    const auto result = resolver.resolve(u8"src/mô đun.vi", true);

    expect(result.exists, "explicit import is found while walking upward");
    expect(result.path == fs::absolute(source).lexically_normal(),
           "explicit import resolves relative to compilation base, not process cwd");
    expect(!result.barePackageCandidate,
           "path import is not classified as a bare package candidate");
}

void testProjectFileWinsBeforePackageFallback() {
    TemporaryTree tree;
    const fs::path resolutionBase = tree.root() / "project" / "nested";
    fs::create_directories(resolutionBase);
    const fs::path localFile = tree.root() / "project" / fs::u8path(u8"lõi.vi");
    const fs::path packageFile = packageMain(tree.root() / "project", u8"lõi");
    writeFile(localFile, "local-file");
    writeFile(packageFile, "package-file");

    const PackageResolver resolver(resolutionBase);
    const auto result = resolver.resolve(u8"lõi", false);

    expect(result.exists && result.barePackageCandidate,
           "bare import is recognized and resolved");
    expect(result.path == fs::absolute(localFile).lexically_normal(),
           "project file keeps precedence over package-directory fallback");
}

void testBareAliasAndQuotedVietnamesePackage() {
    TemporaryTree tree;
    const fs::path project = tree.root() / "project";
    const fs::path resolutionBase = project / "src";
    fs::create_directories(resolutionBase);
    const fs::path coreMain = packageMain(project, u8"lõi");
    const fs::path ioMain = packageMain(project, u8"nhập xuất");
    writeFile(coreMain, "core");
    writeFile(ioMain, "io");

    const PackageResolver resolver(resolutionBase);
    const auto alias = resolver.resolve("vpp_core", false);
    const auto quoted = resolver.resolve(u8"nhập xuất", true);

    expect(alias.path == fs::absolute(coreMain).lexically_normal(),
           "legacy bare alias resolves to canonical Vietnamese package");
    expect(quoted.path == fs::absolute(ioMain).lexically_normal(),
           "quoted package name with spaces resolves through package directory");
}

void testCompatibilityRedirectUsesCanonicalPackageLayout() {
    TemporaryTree tree;
    const fs::path project = tree.root() / "project";
    const fs::path resolutionBase = project / "src";
    fs::create_directories(resolutionBase);
    const fs::path canonical = packageMain(project, u8"mạng");
    writeFile(canonical, "network");

    const PackageResolver resolver(resolutionBase);
    const auto result = resolver.resolve(u8"gói/chuẩn/mạng/main.vi", true);

    expect(result.path == fs::absolute(canonical).lexically_normal(),
           "grouped standard-package compatibility path redirects to canonical layout");
}

void testInstallationHomeFallback() {
    TemporaryTree tree;
    const fs::path project = tree.root() / "project";
    const fs::path installation = tree.root() / "installation";
    fs::create_directories(project);
    const fs::path networkMain = packageMain(installation, u8"mạng");
    const fs::path standardMain =
        installation /
        vietvm::core::utf8Path(vietvm::core::kStandardPackageMainFile);
    writeFile(networkMain, "network");
    writeFile(standardMain, "stdlib");

    const PackageResolver resolver(project, installation);
    const auto package = resolver.resolve(u8"mạng", false);
    const auto standard = resolver.resolve("stdlib", false);

    expect(package.path == fs::absolute(networkMain).lexically_normal(),
           "bare package falls back to explicit installation home");
    expect(standard.path == fs::absolute(standardMain).lexically_normal(),
           "stdlib shortcut falls back to installation home aggregate package");
}

void testMissingImportRetainsCanonicalCandidate() {
    TemporaryTree tree;
    const fs::path project = tree.root() / "project";
    fs::create_directories(project);

    const PackageResolver resolver(project);
    const auto result = resolver.resolve("missing-module", false);
    const fs::path expected =
        fs::absolute(project / "missing-module.vi").lexically_normal();

    expect(!result.exists, "missing import remains unresolved");
    expect(result.path == expected,
           "missing import returns deterministic canonical candidate for diagnostics");
}

} // namespace

int main() {
    testExplicitImportWalksUpwardFromCompilationBase();
    testProjectFileWinsBeforePackageFallback();
    testBareAliasAndQuotedVietnamesePackage();
    testCompatibilityRedirectUsesCanonicalPackageLayout();
    testInstallationHomeFallback();
    testMissingImportRetainsCanonicalCandidate();

    if (failures != 0) {
        std::cerr << failures << " package resolver test(s) failed\n";
        return 1;
    }
    std::cout << "package resolver tests passed\n";
    return 0;
}
