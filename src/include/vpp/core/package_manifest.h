#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vietvm::core {

inline constexpr int kProjectManifestSchemaVersion = 1;

enum class PackageSourceKind {
    Path,
    Git,
    Registry,
};

struct PackageDependencySpec {
    std::string name;
    std::string versionRange = "*";
    PackageSourceKind sourceKind = PackageSourceKind::Path;
    std::string location;
    // Git-only symbolic ref (branch/tag/commit). Canonical manifests use HEAD
    // when omitted so a mutable ref can later be pinned to an exact lock commit.
    std::string reference;
};

struct ProjectManifest {
    int schema = kProjectManifestSchemaVersion;
    std::string name;
    std::string version = "0.1.0";
    std::vector<PackageDependencySpec> dependencies;
};

inline constexpr int kPackageLockSchemaVersion = 1;

struct PackageLockEntry {
    std::string name;
    std::string version;
    PackageSourceKind sourceKind = PackageSourceKind::Path;
    std::string location;
    std::string resolvedPath;
    // Stable content fingerprint for reproducibility/change detection. This is
    // deliberately not a cryptographic integrity signature.
    std::string fingerprint;
    // Git-only exact commit id resolved from the manifest ref. Path/registry
    // entries leave this empty.
    std::string revision;
};

struct PackageLockfile {
    int schema = kPackageLockSchemaVersion;
    std::vector<PackageLockEntry> packages;
};

std::string packageSourceKindName(PackageSourceKind kind);
PackageSourceKind parsePackageSourceKind(const std::string &text);
bool isValidPackageName(std::string_view name) noexcept;

// Đọc/ghi manifest vpp.json theo schema Package 0.9. Reader chấp nhận manifest
// cũ có trường `gói: ["..."]` và nâng nó thành path dependency trong bộ nhớ.
ProjectManifest readProjectManifest(const std::filesystem::path &path);
void writeProjectManifest(const std::filesystem::path &path,
                          const ProjectManifest &manifest);

// Validate semantic version/range, duplicate dependency và field bắt buộc.
void validateProjectManifest(const ProjectManifest &manifest);

PackageLockfile readPackageLockfile(const std::filesystem::path &path);
void writePackageLockfile(const std::filesystem::path &path,
                          const PackageLockfile &lockfile);

// Tạo fingerprint xác định theo relative path + bytes của toàn bộ regular file
// trong package tree. Symlink và directory metadata không tham gia fingerprint.
std::string fingerprintPackageTree(const std::filesystem::path &root);

} // namespace vietvm::core
