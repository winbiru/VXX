#include "vpp/core/package_installer.h"

#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>

#include "vpp/core/package_manifest.h"
#include "vpp/core/project_layout.h"

namespace vietvm::core {
namespace {

namespace fs = std::filesystem;

fs::path absoluteLexical(const fs::path &path) {
    try {
        return fs::absolute(path).lexically_normal();
    } catch (...) {
        return path.lexically_normal();
    }
}

bool isPathInside(const fs::path &child, const fs::path &parent) {
    const fs::path normalizedChild = absoluteLexical(child);
    const fs::path normalizedParent = absoluteLexical(parent);
    auto childIt = normalizedChild.begin();
    auto parentIt = normalizedParent.begin();
    for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt) {
        if (childIt == normalizedChild.end() || *childIt != *parentIt) return false;
    }
    return true;
}

bool isManagedRootEntry(const fs::path &relative) {
    if (relative.empty()) return false;
    const auto first = relative.begin();
    if (first == relative.end()) return false;
    const std::string rootName = first->u8string();
    if (rootName == kPackageStateDirectory) return true;
    if (rootName == kPackageLockFile && std::next(first) == relative.end()) return true;
    return isPackageDirectoryName(rootName);
}

void copyDirectoryTree(const fs::path &source, const fs::path &staging) {
    fs::create_directories(staging);
    for (fs::recursive_directory_iterator it(source), end; it != end; ++it) {
        const fs::directory_entry &entry = *it;
        const fs::path relative = fs::relative(entry.path(), source);
        if (isManagedRootEntry(relative)) {
            if (entry.is_directory()) it.disable_recursion_pending();
            continue;
        }
        const fs::path target = staging / relative;
        if (entry.is_symlink()) {
            throw std::runtime_error(
                "package path không hỗ trợ symlink trong 0.9: " +
                entry.path().u8string());
        }
        if (entry.is_directory()) {
            fs::create_directories(target);
            continue;
        }
        if (!entry.is_regular_file()) {
            throw std::runtime_error(
                "package path chứa entry không được hỗ trợ: " +
                entry.path().u8string());
        }
        fs::create_directories(target.parent_path());
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    }
}

} // namespace

std::string materializePathPackage(
    const std::filesystem::path &source,
    const std::filesystem::path &targetDirectory,
    const std::optional<std::string> &expectedFingerprint) {
    const fs::path sourcePath = absoluteLexical(source);
    const fs::path targetPath = absoluteLexical(targetDirectory);
    if (!fs::exists(sourcePath)) {
        throw std::runtime_error(
            "không tìm thấy package source: " + sourcePath.u8string());
    }

    if (sourcePath == targetPath) {
        if (!fs::is_directory(targetPath)) {
            throw std::runtime_error("package source trùng target nhưng không phải directory");
        }
        const std::string fingerprint = fingerprintPackageTree(targetPath);
        if (expectedFingerprint.has_value() && fingerprint != *expectedFingerprint) {
            throw std::runtime_error(
                "fingerprint package không khớp: mong đợi " + *expectedFingerprint +
                ", thực tế " + fingerprint);
        }
        return fingerprint;
    }

    if (fs::is_directory(sourcePath) && isPathInside(targetPath, sourcePath)) {
        throw std::runtime_error(
            "không thể cài package vào bên trong chính source tree: " +
            targetPath.u8string());
    }

    fs::create_directories(targetPath.parent_path());
    const fs::path staging = targetPath.parent_path() /
                             fs::u8path(".vpp-stage-" +
                                        targetPath.filename().u8string());
    std::error_code ignored;
    fs::remove_all(staging, ignored);

    try {
        if (fs::is_directory(sourcePath)) {
            copyDirectoryTree(sourcePath, staging);
        } else if (fs::is_regular_file(sourcePath)) {
            fs::create_directories(staging);
            fs::copy_file(sourcePath,
                          staging / utf8Path(kPackageEntryFile),
                          fs::copy_options::overwrite_existing);
        } else {
            throw std::runtime_error(
                "package source không phải file hoặc directory: " +
                sourcePath.u8string());
        }

        const std::string fingerprint = fingerprintPackageTree(staging);
        if (expectedFingerprint.has_value() && fingerprint != *expectedFingerprint) {
            throw std::runtime_error(
                "fingerprint package không khớp: mong đợi " + *expectedFingerprint +
                ", thực tế " + fingerprint);
        }

        fs::remove_all(targetPath, ignored);
        if (ignored) {
            throw std::runtime_error(
                "không thể thay package cũ: " + targetPath.u8string() +
                " (" + ignored.message() + ")");
        }
        fs::rename(staging, targetPath);
        return fingerprint;
    } catch (...) {
        fs::remove_all(staging, ignored);
        throw;
    }
}

} // namespace vietvm::core
