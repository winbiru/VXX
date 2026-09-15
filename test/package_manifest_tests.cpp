#include "vpp/core/package_manifest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using vietvm::core::PackageDependencySpec;
using vietvm::core::PackageLockEntry;
using vietvm::core::PackageLockfile;
using vietvm::core::PackageSourceKind;
using vietvm::core::ProjectManifest;

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
                ("vpp-package-manifest-" + std::to_string(stamp));
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

std::string readText(const fs::path &path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void writeText(const fs::path &path, const std::string &text) {
    std::ofstream output(path);
    output << text;
}

void testRoundTripAndDeterministicDependencyOrder() {
    TemporaryTree tree;
    const fs::path path = tree.root() / "vpp.json";
    ProjectManifest manifest;
    manifest.name = u8"ứng dụng thử";
    manifest.version = "1.2.3-beta.1";
    manifest.dependencies = {
        PackageDependencySpec{"zeta", "^2.0.0", PackageSourceKind::Registry, ""},
        PackageDependencySpec{u8"mạng nội bộ", "~1.4.0", PackageSourceKind::Path,
                              "../network"},
    };

    vietvm::core::writeProjectManifest(path, manifest);
    const std::string first = readText(path);
    const ProjectManifest parsed = vietvm::core::readProjectManifest(path);
    vietvm::core::writeProjectManifest(path, parsed);
    const std::string second = readText(path);

    expect(first == second,
           "manifest serialization is deterministic across read/write round-trip");
    expect(parsed.name == manifest.name && parsed.version == manifest.version,
           "manifest preserves project identity and semantic version");
    expect(parsed.dependencies.size() == 2,
           "manifest preserves dependency count");
    expect(first.find(u8"mạng nội bộ") < first.find("zeta"),
           "manifest writer sorts dependencies by name");
}

void testLegacyPackageArrayIsUpgradedInMemory() {
    TemporaryTree tree;
    const fs::path path = tree.root() / "vpp.json";
    writeText(path,
              u8"{\n  \"name\": \"legacy\",\n  \"version\": \"0.1.0\",\n"
              u8"  \"gói\": [\"mạng\", \"nhập xuất\"]\n}\n");

    const ProjectManifest manifest = vietvm::core::readProjectManifest(path);
    expect(manifest.dependencies.size() == 2,
           "legacy gói array is converted to dependencies");
    if (manifest.dependencies.size() == 2) {
        expect(manifest.dependencies[0].sourceKind == PackageSourceKind::Path &&
                   manifest.dependencies[0].versionRange == "*",
               "legacy package becomes wildcard path dependency");
    }
}

void testValidationRejectsBadVersionRangeAndDuplicateName() {
    ProjectManifest badRange;
    badRange.name = "bad-range";
    badRange.dependencies.push_back(
        PackageDependencySpec{"dep", "^", PackageSourceKind::Registry, ""});
    bool rejectedRange = false;
    try {
        vietvm::core::validateProjectManifest(badRange);
    } catch (const std::runtime_error &) {
        rejectedRange = true;
    }
    expect(rejectedRange, "manifest rejects malformed dependency version range");

    ProjectManifest duplicate;
    duplicate.name = "duplicate";
    duplicate.dependencies = {
        PackageDependencySpec{"dep", "*", PackageSourceKind::Registry, ""},
        PackageDependencySpec{"dep", "1.0.0", PackageSourceKind::Registry, ""},
    };
    bool rejectedDuplicate = false;
    try {
        vietvm::core::validateProjectManifest(duplicate);
    } catch (const std::runtime_error &) {
        rejectedDuplicate = true;
    }
    expect(rejectedDuplicate, "manifest rejects duplicate dependency names");
}

void testJsonUnicodeEscapeAndUnknownFields() {
    TemporaryTree tree;
    const fs::path path = tree.root() / "vpp.json";
    writeText(path,
              "{\"schema\":1,\"name\":\"V\\u002b\\u002b\",\"version\":\"1.0.0\","
              "\"unknown\":true,\"dependencies\":[]}");
    const ProjectManifest manifest = vietvm::core::readProjectManifest(path);
    expect(manifest.name == "V++",
           "manifest parser decodes JSON Unicode escapes and ignores unknown fields");
}

void testLockfileRoundTripAndTreeFingerprint() {
    TemporaryTree tree;
    const fs::path packageA = tree.root() / "pkg-a";
    const fs::path packageB = tree.root() / "pkg-b";
    fs::create_directories(packageA / "nested");
    fs::create_directories(packageB);
    writeText(packageA / "main.vi", "hàm main() { trả về 1; }\n");
    writeText(packageA / "nested" / "data.txt", "alpha\n");
    writeText(packageB / "main.vi", "hàm main() { trả về 2; }\n");

    const std::string firstFingerprint =
        vietvm::core::fingerprintPackageTree(packageA);
    const std::string repeatedFingerprint =
        vietvm::core::fingerprintPackageTree(packageA);
    expect(firstFingerprint == repeatedFingerprint,
           "package tree fingerprint is deterministic for unchanged content");
    writeText(packageA / "nested" / "data.txt", "beta\n");
    const std::string changedFingerprint =
        vietvm::core::fingerprintPackageTree(packageA);
    expect(firstFingerprint != changedFingerprint,
           "package tree fingerprint changes when package bytes change");

    PackageLockfile lockfile;
    lockfile.packages = {
        PackageLockEntry{"zeta", "2.0.0", PackageSourceKind::Registry, "", "gói/zeta",
                         vietvm::core::fingerprintPackageTree(packageB)},
        PackageLockEntry{"alpha", "1.2.3", PackageSourceKind::Path, "../alpha", "gói/alpha",
                         changedFingerprint},
    };
    const fs::path lockPath = tree.root() / "vpp.lock";
    vietvm::core::writePackageLockfile(lockPath, lockfile);
    const std::string first = readText(lockPath);
    const PackageLockfile parsed = vietvm::core::readPackageLockfile(lockPath);
    vietvm::core::writePackageLockfile(lockPath, parsed);
    const std::string second = readText(lockPath);

    expect(first == second,
           "lockfile serialization is deterministic across read/write round-trip");
    expect(parsed.packages.size() == 2,
           "lockfile preserves all resolved packages");
    expect(first.find("alpha") < first.find("zeta"),
           "lockfile writer sorts packages by name");
}

} // namespace

int main() {
    testRoundTripAndDeterministicDependencyOrder();
    testLegacyPackageArrayIsUpgradedInMemory();
    testValidationRejectsBadVersionRangeAndDuplicateName();
    testJsonUnicodeEscapeAndUnknownFields();
    testLockfileRoundTripAndTreeFingerprint();

    if (failures != 0) {
        std::cerr << failures << " package manifest test(s) failed\n";
        return 1;
    }
    std::cout << "package manifest tests passed\n";
    return 0;
}
