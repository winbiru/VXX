#include "vpp/core/package_solver.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "vpp/core/project_layout.h"
#include "vpp/core/semver.h"

namespace vietvm::core {
namespace {

namespace fs = std::filesystem;

fs::path absoluteLexical(const fs::path &base, const fs::path &candidate) {
    fs::path resolved = candidate.is_absolute() ? candidate : base / candidate;
    try {
        return fs::absolute(resolved).lexically_normal();
    } catch (...) {
        return resolved.lexically_normal();
    }
}

std::string joinCycle(const std::vector<std::string> &stack,
                      const std::string &name) {
    std::ostringstream out;
    bool started = false;
    for (const std::string &item : stack) {
        if (!started && item == name) started = true;
        if (!started) continue;
        if (out.tellp() > 0) out << " -> ";
        out << item;
    }
    if (out.tellp() > 0) out << " -> ";
    out << name;
    return out.str();
}

struct SourceMetadata {
    std::string version = "0.0.0+local";
    std::vector<PackageDependencySpec> dependencies;
};

SourceMetadata readSourceMetadata(const fs::path &sourcePath) {
    SourceMetadata metadata;
    if (!fs::is_directory(sourcePath)) return metadata;
    const fs::path manifestPath =
        sourcePath / utf8Path(kProjectManifestFile);
    if (!fs::exists(manifestPath)) return metadata;
    const ProjectManifest manifest = readProjectManifest(manifestPath);
    metadata.version = manifest.version;
    metadata.dependencies = manifest.dependencies;
    return metadata;
}

class Solver {
public:
    explicit Solver(fs::path projectRoot)
        : projectRoot_(absoluteLexical(fs::current_path(), std::move(projectRoot))) {}

    ResolvedPackageGraph solve(const ProjectManifest &manifest) {
        std::vector<PackageDependencySpec> roots = manifest.dependencies;
        sortDependencies(roots);
        for (const auto &dependency : roots) {
            resolveDependency(dependency, projectRoot_, manifest.name);
        }
        return ResolvedPackageGraph{std::move(ordered_)};
    }

private:
    static void sortDependencies(std::vector<PackageDependencySpec> &dependencies) {
        std::sort(dependencies.begin(), dependencies.end(),
                  [](const PackageDependencySpec &left,
                     const PackageDependencySpec &right) {
                      if (left.name != right.name) return left.name < right.name;
                      if (left.versionRange != right.versionRange) {
                          return left.versionRange < right.versionRange;
                      }
                      return left.location < right.location;
                  });
    }

    void validateRange(const std::string &name,
                       const std::string &requester,
                       const std::string &rangeText,
                       const std::string &versionText) const {
        std::string rangeError;
        const auto range = VersionRange::parse(rangeText, &rangeError);
        if (!range.has_value()) {
            throw std::runtime_error(
                "dependency '" + name + "' từ '" + requester +
                "' có version range không hợp lệ '" + rangeText +
                "': " + rangeError);
        }
        std::string versionError;
        const auto version = SemanticVersion::parse(versionText, &versionError);
        if (!version.has_value()) {
            throw std::runtime_error(
                "dependency '" + name + "' có source version không hợp lệ '" +
                versionText + "': " + versionError);
        }
        if (!range->matches(*version)) {
            throw std::runtime_error(
                "xung đột dependency '" + name + "': phiên bản " + versionText +
                " không thỏa range " + rangeText + " do '" + requester + "' yêu cầu");
        }
    }

    void resolveDependency(const PackageDependencySpec &dependency,
                           const fs::path &declaringRoot,
                           const std::string &requester) {
        if (dependency.sourceKind != PackageSourceKind::Path) {
            throw std::runtime_error(
                "dependency '" + dependency.name + "' dùng source '" +
                packageSourceKindName(dependency.sourceKind) +
                "' nhưng transport này chưa được hỗ trợ trong Package 0.9");
        }

        const fs::path sourcePath = absoluteLexical(
            declaringRoot, utf8Path(dependency.location));
        if (!fs::exists(sourcePath)) {
            throw std::runtime_error(
                "không tìm thấy source path của dependency '" + dependency.name +
                "': " + sourcePath.u8string());
        }
        if (!fs::is_directory(sourcePath) && !fs::is_regular_file(sourcePath)) {
            throw std::runtime_error(
                "source path của dependency '" + dependency.name +
                "' không phải file hoặc directory: " + sourcePath.u8string());
        }

        const SourceMetadata metadata = readSourceMetadata(sourcePath);
        validateRange(dependency.name, requester,
                      dependency.versionRange, metadata.version);

        const auto existing = indexByName_.find(dependency.name);
        if (existing != indexByName_.end()) {
            ResolvedPackageDependency &resolved = ordered_[existing->second];
            if (resolved.sourcePath != sourcePath || resolved.version != metadata.version) {
                throw std::runtime_error(
                    "xung đột dependency '" + dependency.name +
                    "': đã resolve " + resolved.version + " từ " +
                    resolved.sourcePath.u8string() + ", nhưng '" + requester +
                    "' yêu cầu source " + sourcePath.u8string() +
                    " phiên bản " + metadata.version);
            }
            if (std::find(resolved.requestedRanges.begin(),
                          resolved.requestedRanges.end(),
                          dependency.versionRange) == resolved.requestedRanges.end()) {
                resolved.requestedRanges.push_back(dependency.versionRange);
                std::sort(resolved.requestedRanges.begin(), resolved.requestedRanges.end());
            }
            return;
        }

        if (visiting_.count(dependency.name) != 0) {
            throw std::runtime_error(
                "phát hiện vòng lặp dependency: " + joinCycle(stack_, dependency.name));
        }

        visiting_.insert(dependency.name);
        stack_.push_back(dependency.name);

        std::vector<PackageDependencySpec> children = metadata.dependencies;
        sortDependencies(children);
        const fs::path childBase =
            fs::is_directory(sourcePath) ? sourcePath : sourcePath.parent_path();
        for (const auto &child : children) {
            resolveDependency(child, childBase, dependency.name);
        }

        stack_.pop_back();
        visiting_.erase(dependency.name);

        ResolvedPackageDependency resolved;
        resolved.name = dependency.name;
        resolved.version = metadata.version;
        resolved.sourceKind = dependency.sourceKind;
        resolved.declaredLocation = dependency.location;
        resolved.sourcePath = sourcePath;
        resolved.requestedRanges.push_back(dependency.versionRange);
        indexByName_[resolved.name] = ordered_.size();
        ordered_.push_back(std::move(resolved));
    }

    fs::path projectRoot_;
    std::vector<ResolvedPackageDependency> ordered_;
    std::unordered_map<std::string, std::size_t> indexByName_;
    std::unordered_set<std::string> visiting_;
    std::vector<std::string> stack_;
};

} // namespace

ResolvedPackageGraph resolvePackageDependencyGraph(
    const ProjectManifest &manifest,
    const std::filesystem::path &projectRoot) {
    validateProjectManifest(manifest);
    return Solver(projectRoot).solve(manifest);
}

} // namespace vietvm::core
