#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "vpp/core/package_manifest.h"
#include "vpp/core/package_source.h"

namespace vietvm::core {

// Một dependency đã được solver gắn với source cụ thể và exact version. Danh
// sách trả về theo thứ tự dependency-first để installer có thể materialize graph
// mà không phải tự duyệt lại manifest con.
struct ResolvedPackageDependency {
    std::string name;
    std::string version;
    PackageSourceKind sourceKind = PackageSourceKind::Path;
    std::string declaredLocation;
    std::filesystem::path sourcePath;
    std::string resolvedLocation;
    std::string sourceRevision;
    std::vector<std::string> requestedRanges;
};

struct ResolvedPackageGraph {
    std::vector<ResolvedPackageDependency> packages;
};

using PackageSourceMaterializer = std::function<MaterializedPackageSource(
    const PackageDependencySpec &dependency,
    const std::filesystem::path &declaringRoot)>;

// Giải dependency graph từ manifest gốc. Transport được inject để solver chỉ
// xử lý graph/version/cycle, không tự clone/fetch. Khi không truyền materializer,
// behavior tương thích cũ chỉ resolve local `path` và từ chối remote source.
ResolvedPackageGraph resolvePackageDependencyGraph(
    const ProjectManifest &manifest,
    const std::filesystem::path &projectRoot,
    PackageSourceMaterializer materializer = {});

} // namespace vietvm::core
