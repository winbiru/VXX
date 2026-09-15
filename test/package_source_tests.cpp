#include "vpp/core/package_source.h"
#include "vpp/core/package_solver.h"
#include "vpp/core/project_layout.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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
                ("vpp-package-source-" + std::to_string(stamp));
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

void writeProject(const fs::path &root,
                  const std::string &name,
                  const std::string &version,
                  std::vector<PackageDependencySpec> dependencies = {}) {
    fs::create_directories(root);
    ProjectManifest manifest;
    manifest.name = name;
    manifest.version = version;
    manifest.dependencies = std::move(dependencies);
    vietvm::core::writeProjectManifest(root / "vpp.json", manifest);
    writeText(root / "main.vi",
              "hàm registry_value() { trả về " + version.substr(0, 1) + "; }\n");
}

PackageDependencySpec registryDependency(const std::string &name,
                                         const std::string &range,
                                         const std::string &location) {
    return PackageDependencySpec{
        name, range, PackageSourceKind::Registry, location, ""};
}

void testRegistrySelectsHighestMatchingVersionAndIsImmutable() {
    TemporaryTree tree;
    const fs::path registry = tree.root() / "registry";
    const fs::path source100 = tree.root() / "source-1.0.0";
    const fs::path source150 = tree.root() / "source-1.5.0";
    const fs::path source200 = tree.root() / "source-2.0.0";
    writeProject(source100, "demo", "1.0.0");
    writeProject(source150, "demo", "1.5.0");
    writeProject(source200, "demo", "2.0.0");

    const auto firstPublish =
        vietvm::core::publishPackageToRegistry(source100, registry);
    const auto repeatedPublish =
        vietvm::core::publishPackageToRegistry(source100, registry);
    (void)vietvm::core::publishPackageToRegistry(source150, registry);
    (void)vietvm::core::publishPackageToRegistry(source200, registry);
    expect(!firstPublish.alreadyPublished && repeatedPublish.alreadyPublished,
           "publishing identical registry version is idempotent");
    expect(fs::exists(registry / vietvm::core::utf8Path(
                                 vietvm::core::kRegistryMarkerFile)),
           "publish creates registry schema marker");

    const auto selected = vietvm::core::materializePackageSource(
        registryDependency("demo", "^1.0.0", registry.u8string()),
        tree.root(), tree.root() / "state");
    const auto selectedManifest = vietvm::core::readProjectManifest(
        selected.path / vietvm::core::utf8Path(vietvm::core::kProjectManifestFile));
    expect(selectedManifest.version == "1.5.0",
           "registry selects highest SemVer matching the declared range");
    expect(selected.resolvedLocation == fs::absolute(registry).lexically_normal().generic_u8string(),
           "registry materializer returns canonical registry root for lock persistence");

    writeText(source150 / "main.vi", "hàm registry_value() { trả về 99; }\n");
    bool rejectedMutation = false;
    try {
        (void)vietvm::core::publishPackageToRegistry(source150, registry);
    } catch (const std::runtime_error &) {
        rejectedMutation = true;
    }
    expect(rejectedMutation,
           "registry rejects different bytes for an already published version");
}

void testTransitiveRegistryDependencyInheritsRegistryRoot() {
    TemporaryTree tree;
    const fs::path registry = tree.root() / "registry";
    const fs::path child = tree.root() / "child";
    const fs::path parent = tree.root() / "parent";
    writeProject(child, "child", "1.1.0");
    writeProject(parent, "parent", "2.0.0",
                 {registryDependency("child", "^1.0.0", "")});
    (void)vietvm::core::publishPackageToRegistry(child, registry);
    (void)vietvm::core::publishPackageToRegistry(parent, registry);

    ProjectManifest root;
    root.name = "app";
    root.dependencies = {
        registryDependency("parent", "^2.0.0", registry.u8string()),
    };
    const fs::path state = tree.root() / "state";
    const auto graph = vietvm::core::resolvePackageDependencyGraph(
        root, tree.root(),
        [&](const PackageDependencySpec &dependency, const fs::path &declaringRoot) {
            return vietvm::core::materializePackageSource(
                dependency, declaringRoot, state);
        });
    expect(graph.packages.size() == 2,
           "registry solver resolves transitive package from same registry");
    if (graph.packages.size() == 2) {
        expect(graph.packages[0].name == "child" &&
                   graph.packages[1].name == "parent",
               "transitive registry graph remains dependency-first");
        expect(graph.packages[0].version == "1.1.0" &&
                   graph.packages[1].version == "2.0.0",
               "registry graph preserves selected exact versions");
    }
}

} // namespace

int main() {
    testRegistrySelectsHighestMatchingVersionAndIsImmutable();
    testTransitiveRegistryDependencyInheritsRegistryRoot();
    if (failures != 0) {
        std::cerr << failures << " package source test(s) failed\n";
        return 1;
    }
    std::cout << "package source tests passed\n";
    return 0;
}
