#include "vpp/core/package_cache.h"
#include "vpp/core/package_manifest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
                ("vpp-package-cache-" + std::to_string(stamp));
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

void testContentAddressedCache() {
    TemporaryTree tree;
    const fs::path installed = tree.root() / "project" / "gói" / "demo";
    writeText(installed / "main.vi", "hàm main() { trả về 1; }\n");
    const std::string fingerprint =
        vietvm::core::fingerprintPackageTree(installed);
    const fs::path cacheRoot =
        vietvm::core::packageCacheRoot(tree.root() / "project");

    const fs::path cached = vietvm::core::cachePackageTree(
        installed, cacheRoot, fingerprint);
    expect(fs::exists(cached / "main.vi"),
           "cache snapshots package content under fingerprint key");
    const auto found = vietvm::core::findCachedPackage(cacheRoot, fingerprint);
    expect(found.has_value() && *found == cached,
           "cache lookup returns verified content-addressed entry");

    writeText(cached / "main.vi", "tampered\n");
    expect(!vietvm::core::findCachedPackage(cacheRoot, fingerprint).has_value(),
           "cache lookup rejects entry whose bytes no longer match fingerprint");
}

} // namespace

int main() {
    testContentAddressedCache();
    if (failures != 0) {
        std::cerr << failures << " package cache test(s) failed\n";
        return 1;
    }
    std::cout << "package cache tests passed\n";
    return 0;
}
