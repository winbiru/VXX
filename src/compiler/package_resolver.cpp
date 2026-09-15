#include "vpp/compiler/package_resolver.h"

#include <filesystem>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "vpp/core/project_layout.h"

namespace vietvm::compiler {
namespace {

namespace fs = std::filesystem;

const std::unordered_map<std::string, std::string> &packageAliases() {
    static const std::unordered_map<std::string, std::string> aliases = {
        {"vpp_core", "lõi"},
        {"cốt lõi", "lõi"},
        {"vpp_io", "nhập xuất"},
        {"vào ra", "nhập xuất"},
        {"vpp_http", "mạng"},
        {"vpp_web", "mạng"},
        {"mạng web", "mạng"},
        {"vpp_system", "hệ thống"},
        {"vpp_data", "dữ liệu"},
        {"vpp_app", "ứng dụng"},
        {"vpp_starters", "dựng"},
        {"khởi động", "dựng"},
    };
    return aliases;
}

const std::unordered_set<std::string> &bundledPackageNames() {
    static const std::unordered_set<std::string> names = {
        "lõi",
        "nhập xuất",
        "mạng",
        "hệ thống",
        "dữ liệu",
        "ứng dụng",
        "dựng",
        "kiểm thử",
    };
    return names;
}

const std::unordered_map<std::string, std::string> &compatibilityModuleRedirects() {
    static const std::unordered_map<std::string, std::string> redirects = {
        {"chuẩn/hỗ trợ/nhật ký.vi", "nhập xuất/nhật ký.vi"},
        {"chuẩn/hỗ trợ/xác thực.vi", "lõi/xác thực.vi"},
        {"ứng dụng/tương thích/api.vi", "ứng dụng/cầu nối/api.vi"},
        {"chuẩn/ứng dụng/tương thích/api.vi", "ứng dụng/cầu nối/api.vi"},
        {"khởi động/khởi động web.vi", "dựng/web.vi"},
        {"khởi động/khởi động dữ liệu.vi", "dựng/dữ liệu.vi"},
        {"khởi động/khởi động ứng dụng.vi", "dựng/ứng dụng.vi"},
        {"dựng/khởi động web.vi", "dựng/web.vi"},
        {"dựng/khởi động dữ liệu.vi", "dựng/dữ liệu.vi"},
        {"dựng/khởi động ứng dụng.vi", "dựng/ứng dụng.vi"},
        {"chuẩn/khởi động/khởi động web.vi", "dựng/web.vi"},
        {"chuẩn/khởi động/khởi động dữ liệu.vi", "dựng/dữ liệu.vi"},
        {"chuẩn/khởi động/khởi động ứng dụng.vi", "dựng/ứng dụng.vi"},
    };
    return redirects;
}

fs::path compatibilityRedirectFor(const fs::path &requestedPath) {
    const fs::path normalized = requestedPath.lexically_normal();
    auto root = normalized.begin();
    if (root == normalized.end()) return {};

    const std::string rootName = root->u8string();
    if (!vietvm::core::isPackageDirectoryName(rootName)) return {};

    fs::path relativePath;
    for (auto item = std::next(root); item != normalized.end(); ++item) {
        relativePath /= *item;
    }

    const auto leaf = compatibilityModuleRedirects().find(
        relativePath.generic_u8string());
    if (leaf != compatibilityModuleRedirects().end()) {
        return vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
               vietvm::core::utf8Path(leaf->second);
    }

    auto package = relativePath.begin();
    bool groupedStandardPackages = false;
    if (package != relativePath.end() &&
        package->u8string() == vietvm::core::kStandardPackageDirectory) {
        groupedStandardPackages = true;
        ++package;
    }

    if (groupedStandardPackages && package != relativePath.end() &&
        package->u8string() == vietvm::core::kPackageEntryFile) {
        return vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
               vietvm::core::utf8Path(vietvm::core::kStandardPackageDirectory) /
               vietvm::core::utf8Path(vietvm::core::kPackageEntryFile);
    }

    if (package == relativePath.end()) return {};

    const std::string packageName = package->u8string();
    std::string canonicalPackage;
    const auto alias = packageAliases().find(packageName);
    if (alias != packageAliases().end()) {
        canonicalPackage = alias->second;
    } else if (bundledPackageNames().find(packageName) !=
               bundledPackageNames().end()) {
        canonicalPackage = packageName;
    }
    if (canonicalPackage.empty()) return {};

    fs::path redirect =
        vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
        vietvm::core::utf8Path(canonicalPackage);
    for (auto rest = std::next(package); rest != relativePath.end(); ++rest) {
        redirect /= *rest;
    }
    return redirect;
}

std::vector<std::string> packageCandidatesFor(const std::string &bareModule) {
    std::vector<std::string> candidates;
    candidates.push_back(bareModule);
    const auto alias = packageAliases().find(bareModule);
    if (alias != packageAliases().end()) {
        candidates.push_back(alias->second);
    }
    return candidates;
}

} // namespace

PackageResolver::PackageResolver(
    fs::path resolutionBase,
    std::optional<fs::path> installationHome)
    : resolutionBase_(std::move(resolutionBase)),
      installationHome_(std::move(installationHome)) {
    if (resolutionBase_.empty()) resolutionBase_ = fs::current_path();
    try {
        resolutionBase_ = fs::absolute(resolutionBase_).lexically_normal();
    } catch (...) {
        resolutionBase_ = resolutionBase_.lexically_normal();
    }
    if (installationHome_.has_value()) {
        installationHome_ = absoluteLexical(*installationHome_);
    }
}

fs::path PackageResolver::absoluteLexical(const fs::path &candidate) const {
    fs::path resolved = candidate.is_absolute()
                            ? candidate
                            : resolutionBase_ / candidate;
    try {
        return fs::absolute(resolved).lexically_normal();
    } catch (...) {
        return resolved.lexically_normal();
    }
}

PackageResolution PackageResolver::resolve(
    const std::string &target,
    bool quoted) const {
    std::string path = target;
    if (path == "stdlib" || path == "chuẩn") {
        path = vietvm::core::kStandardPackageMainFile;
    }

    const fs::path requestedPath = vietvm::core::utf8Path(path);
    const bool bareModuleName = requestedPath.parent_path().empty() &&
                                requestedPath.extension().empty();
    const std::vector<std::string> packageCandidates =
        bareModuleName ? packageCandidatesFor(path) : std::vector<std::string>{};

    if (!quoted && requestedPath.extension().empty()) {
        path += ".vi";
    }
    const fs::path importPath = vietvm::core::utf8Path(path);
    const fs::path compatibilityRedirect = compatibilityRedirectFor(importPath);

    fs::path resolved = absoluteLexical(importPath);

    const auto resolvePackageAtBase = [&](const fs::path &base,
                                          const std::string &packageName,
                                          fs::path &result) {
        const fs::path packagePath = vietvm::core::utf8Path(packageName);
        const fs::path packageRoot = base / packagePath;
        const fs::path packageMain = vietvm::core::packageEntryPath(packageRoot);
        const fs::path packageSource =
            base / vietvm::core::utf8Path(packageName + ".vi");
        if (fs::exists(packageMain)) {
            result = absoluteLexical(packageMain);
            return true;
        }
        if (fs::exists(packageRoot) && fs::is_regular_file(packageRoot)) {
            result = absoluteLexical(packageRoot);
            return true;
        }
        if (fs::exists(packageSource)) {
            result = absoluteLexical(packageSource);
            return true;
        }
        return false;
    };

    if (!fs::exists(resolved)) {
        for (fs::path dir = resolutionBase_;; dir = dir.parent_path()) {
            const fs::path candidate = dir / importPath;
            if (fs::exists(candidate)) {
                resolved = absoluteLexical(candidate);
                break;
            }

            if (!compatibilityRedirect.empty()) {
                const fs::path redirected = dir / compatibilityRedirect;
                if (fs::exists(redirected)) {
                    resolved = absoluteLexical(redirected);
                    break;
                }
            }

            if (bareModuleName) {
                bool foundPackage = false;
                for (const char *packageDirectory :
                     vietvm::core::kPackageDirectoryNames) {
                    const fs::path base =
                        dir / vietvm::core::utf8Path(packageDirectory);
                    for (const auto &packageName : packageCandidates) {
                        if (resolvePackageAtBase(base, packageName, resolved)) {
                            foundPackage = true;
                            break;
                        }
                    }
                    if (foundPackage) break;
                }
                if (foundPackage) break;
            }

            if (dir == dir.parent_path()) break;
        }
    }

    if (!fs::exists(resolved) && installationHome_.has_value()) {
        const fs::path &home = *installationHome_;
        const fs::path bundled = home / importPath;
        if (fs::exists(bundled)) {
            resolved = absoluteLexical(bundled);
        }

        if (!fs::exists(resolved) && !compatibilityRedirect.empty()) {
            const fs::path redirected = home / compatibilityRedirect;
            if (fs::exists(redirected)) {
                resolved = absoluteLexical(redirected);
            }
        }

        if (!fs::exists(resolved) && bareModuleName) {
            bool foundPackage = false;
            for (const char *packageDirectory :
                 vietvm::core::kPackageDirectoryNames) {
                const fs::path base =
                    home / vietvm::core::utf8Path(packageDirectory);
                for (const auto &packageName : packageCandidates) {
                    if (resolvePackageAtBase(base, packageName, resolved)) {
                        foundPackage = true;
                        break;
                    }
                }
                if (foundPackage) break;
            }
        }
    }

    return PackageResolution{
        resolved.lexically_normal(), fs::exists(resolved), bareModuleName};
}

} // namespace vietvm::compiler
