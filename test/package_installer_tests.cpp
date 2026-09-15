#include "vpp/core/package_installer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
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
                ("vpp-package-installer-" + std::to_string(stamp));
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

void writeText(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
}

std::string readText(const fs::path &path) {
    std::ifstream input(path);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

void testStagedInstallAndFingerprintGuard() {
    TemporaryTree tree;
    const fs::path source = tree.root() / "source";
    const fs::path target = tree.root() / "project" / "gói" / "demo";
    writeText(source / "main.vi", "hàm main() { trả về 1; }\n");
    writeText(source / "data" / "a.txt", "alpha\n");

    const std::string first =
        vietvm::core::materializePathPackage(source, target);
    expect(fs::exists(target / "main.vi") && fs::exists(target / "data" / "a.txt"),
           "staged installer materializes the entire directory tree");

    writeText(source / "main.vi", "hàm main() { trả về 2; }\n");
    bool rejected = false;
    try {
        (void)vietvm::core::materializePathPackage(source, target, first);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    expect(rejected, "expected fingerprint rejects changed source before commit");
    expect(readText(target / "main.vi") == "hàm main() { trả về 1; }\n",
           "fingerprint mismatch leaves previous installed package untouched");

    const std::string second =
        vietvm::core::materializePathPackage(source, target);
    expect(second != first,
           "successful reinstall commits changed package content and fingerprint");
    expect(readText(target / "main.vi") == "hàm main() { trả về 2; }\n",
           "successful reinstall atomically replaces package directory");
}

void testSingleFileSourceBecomesMain() {
    TemporaryTree tree;
    const fs::path source = tree.root() / "single.vi";
    const fs::path target = tree.root() / "project" / "gói" / "single";
    writeText(source, "hàm single() { trả về 9; }\n");

    (void)vietvm::core::materializePathPackage(source, target);
    expect(readText(target / "main.vi") == "hàm single() { trả về 9; }\n",
           "single-file package source is materialized as main.vi");
}

void testManagedPackageStateIsNotVendored() {
    TemporaryTree tree;
    const fs::path source = tree.root() / "source";
    const fs::path target = tree.root() / "project" / "gói" / "clean";
    writeText(source / "main.vi", "hàm clean() { trả về 1; }\n");
    writeText(source / "vpp.lock", "generated-lock");
    writeText(source / ".vpp" / "cache" / "blob", "cached");
    writeText(source / "gói" / "nested" / "main.vi", "nested-installed-dependency");

    (void)vietvm::core::materializePathPackage(source, target);
    expect(fs::exists(target / "main.vi"),
           "package source files remain in vendored package");
    expect(!fs::exists(target / "vpp.lock") &&
               !fs::exists(target / ".vpp") &&
               !fs::exists(target / "gói"),
           "generated lock/cache/installed dependency trees are excluded from package artifact");
}

} // namespace

int main() {
    testStagedInstallAndFingerprintGuard();
    testSingleFileSourceBecomesMain();
    testManagedPackageStateIsNotVendored();
    if (failures != 0) {
        std::cerr << failures << " package installer test(s) failed\n";
        return 1;
    }
    std::cout << "package installer tests passed\n";
    return 0;
}
