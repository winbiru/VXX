#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vpp/core/package_manifest.h"

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
    std::vector<std::string> requestedRanges;
};

struct ResolvedPackageGraph {
    std::vector<ResolvedPackageDependency> packages;
};

// Giải dependency graph từ manifest gốc. Package 0.9 hiện resolve được source
// `path`; Git/registry có schema nhưng chưa có fetch transport nên solver từ chối
// chúng thay vì âm thầm chọn source khác. Conflict cùng tên/path/version và cycle
// đều tạo diagnostic xác định.
ResolvedPackageGraph resolvePackageDependencyGraph(
    const ProjectManifest &manifest,
    const std::filesystem::path &projectRoot);

} // namespace vietvm::core
