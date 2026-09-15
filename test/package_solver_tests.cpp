#include "vpp/core/package_solver.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using vietvm::core::PackageDependencySpec;
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
                ("vpp-package-solver-" + std::to_string(stamp));
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

void writePackage(const fs::path &root,
                  const std::string &name,
                  const std::string &version,
                  std::vector<PackageDependencySpec> dependencies = {}) {
    fs::create_directories(root);
    ProjectManifest manifest;
    manifest.name = name;
    manifest.version = version;
    manifest.dependencies = std::move(dependencies);
    vietvm::core::writeProjectManifest(root / "vpp.json", manifest);
}

PackageDependencySpec pathDependency(const std::string &name,
                                     const std::string &range,
                                     const std::string &location) {
    return PackageDependencySpec{name, range, PackageSourceKind::Path, location};
}

void testTransitiveGraphIsDependencyFirstAndDeterministic() {
    TemporaryTree tree;
    const fs::path packages = tree.root() / "sources";
    writePackage(packages / "shared", "shared", "1.4.0");
    writePackage(packages / "alpha", "alpha", "2.0.0",
                 {pathDependency("shared", "^1.0.0", "../shared")});
    writePackage(packages / "beta", "beta", "3.0.0",
                 {pathDependency("shared", ">=1.2.0 <2.0.0", "../shared")});

    ProjectManifest root;
    root.name = "app";
    root.dependencies = {
        pathDependency("beta", "3.0.0", "sources/beta"),
        pathDependency("alpha", "^2.0.0", "sources/alpha"),
    };

    const auto graph = vietvm::core::resolvePackageDependencyGraph(root, tree.root());
    expect(graph.packages.size() == 3,
           "transitive solver deduplicates shared dependency");
    if (graph.packages.size() == 3) {
        expect(graph.packages[0].name == "shared",
               "dependency is emitted before packages that depend on it");
        expect(graph.packages[1].name == "alpha" &&
                   graph.packages[2].name == "beta",
               "root dependency traversal is deterministic by name");
        expect(graph.packages[0].requestedRanges.size() == 2,
               "shared dependency retains all compatible requested ranges");
    }
}

void testVersionConflictIsRejected() {
    TemporaryTree tree;
    const fs::path packages = tree.root() / "sources";
    writePackage(packages / "shared", "shared", "1.4.0");
    writePackage(packages / "alpha", "alpha", "1.0.0",
                 {pathDependency("shared", "^1.0.0", "../shared")});
    writePackage(packages / "beta", "beta", "1.0.0",
                 {pathDependency("shared", "^2.0.0", "../shared")});

    ProjectManifest root;
    root.name = "app";
    root.dependencies = {
        pathDependency("alpha", "*", "sources/alpha"),
        pathDependency("beta", "*", "sources/beta"),
    };

    bool rejected = false;
    std::string message;
    try {
        (void)vietvm::core::resolvePackageDependencyGraph(root, tree.root());
    } catch (const std::runtime_error &error) {
        rejected = true;
        message = error.what();
    }
    expect(rejected && message.find("xung đột dependency 'shared'") != std::string::npos,
           "incompatible transitive version range is rejected with dependency name");
}

void testCycleIsRejected() {
    TemporaryTree tree;
    const fs::path packages = tree.root() / "sources";
    writePackage(packages / "a", "a", "1.0.0",
                 {pathDependency("b", "*", "../b")});
    writePackage(packages / "b", "b", "1.0.0",
                 {pathDependency("a", "*", "../a")});

    ProjectManifest root;
    root.name = "app";
    root.dependencies = {pathDependency("a", "*", "sources/a")};

    bool rejected = false;
    std::string message;
    try {
        (void)vietvm::core::resolvePackageDependencyGraph(root, tree.root());
    } catch (const std::runtime_error &error) {
        rejected = true;
        message = error.what();
    }
    expect(rejected && message.find("a -> b -> a") != std::string::npos,
           "dependency cycle is rejected with deterministic cycle path");
}

void testUnsupportedTransportIsRejected() {
    TemporaryTree tree;
    ProjectManifest root;
    root.name = "app";
    root.dependencies = {
        PackageDependencySpec{"remote", "^1.0.0", PackageSourceKind::Git,
                              "https://example.invalid/repo.git"},
    };
    bool rejected = false;
    try {
        (void)vietvm::core::resolvePackageDependencyGraph(root, tree.root());
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    expect(rejected,
           "solver rejects Git/registry source until transport is implemented");
}

} // namespace

int main() {
    testTransitiveGraphIsDependencyFirstAndDeterministic();
    testVersionConflictIsRejected();
    testCycleIsRejected();
    testUnsupportedTransportIsRejected();

    if (failures != 0) {
        std::cerr << failures << " package solver test(s) failed\n";
        return 1;
    }
    std::cout << "package solver tests passed\n";
    return 0;
}
