#pragma once

#include <filesystem>
#include <string>

#include "vpp/core/package_manifest.h"

namespace vietvm::core {

struct MaterializedPackageSource {
    std::filesystem::path path;
    // Exact immutable source identity when the transport provides one. Git
    // returns the checked-out commit; path/registry sources leave this empty
    // because registry identity is exact SemVer + content fingerprint.
    std::string revision;
    // Canonical location suitable for persisting in vpp.lock. Local Git paths
    // are made absolute so a transitive package can later be restored without
    // reconstructing the declaring checkout directory.
    std::string resolvedLocation;
};

// Materialize a dependency source outside the solver. `stateRoot` is private
// package-manager state (normally project/.vpp/sources), never the installed
// `gói/` tree. Path sources are only resolved; Git sources are checked out at
// the requested ref; registry sources select the highest version matching the
// declared SemVer range from an immutable registry layout.
MaterializedPackageSource materializePackageSource(
    const PackageDependencySpec &dependency,
    const std::filesystem::path &declaringRoot,
    const std::filesystem::path &stateRoot);

struct PublishedRegistryPackage {
    std::string name;
    std::string version;
    std::filesystem::path path;
    std::string fingerprint;
    bool alreadyPublished = false;
};

// Publish the current package artifact into registry layout
// `<registry>/<name>/<semver>/`. Published versions are immutable: publishing
// identical bytes is idempotent, while different bytes for an existing
// name/version are rejected.
PublishedRegistryPackage publishPackageToRegistry(
    const std::filesystem::path &projectRoot,
    const std::filesystem::path &registryRoot);

} // namespace vietvm::core
