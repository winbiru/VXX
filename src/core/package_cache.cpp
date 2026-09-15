#include "vpp/core/package_cache.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "vpp/core/package_manifest.h"
#include "vpp/core/project_layout.h"

namespace vietvm::core {
namespace {

std::string cacheKeyForFingerprint(const std::string &fingerprint) {
    const std::string prefix = "fnv1a64:";
    if (fingerprint.rfind(prefix, 0) != 0 ||
        fingerprint.size() != prefix.size() + 16) {
        throw std::runtime_error(
            "fingerprint package không hợp lệ cho cache: " + fingerprint);
    }
    const std::string digest = fingerprint.substr(prefix.size());
    const bool valid = std::all_of(
        digest.begin(), digest.end(), [](unsigned char c) {
            return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
        });
    if (!valid) {
        throw std::runtime_error(
            "fingerprint package không hợp lệ cho cache: " + fingerprint);
    }
    return "fnv1a64-" + digest;
}

void copyExactTree(const std::filesystem::path &source,
                   const std::filesystem::path &target) {
    std::filesystem::create_directories(target);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(source)) {
        const std::filesystem::path relative =
            std::filesystem::relative(entry.path(), source);
        const std::filesystem::path destination = target / relative;
        if (entry.is_symlink()) {
            throw std::runtime_error(
                "cache package không hỗ trợ symlink: " + entry.path().u8string());
        }
        if (entry.is_directory()) {
            std::filesystem::create_directories(destination);
        } else if (entry.is_regular_file()) {
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(
                entry.path(), destination,
                std::filesystem::copy_options::overwrite_existing);
        } else {
            throw std::runtime_error(
                "cache package gặp entry không được hỗ trợ: " + entry.path().u8string());
        }
    }
}

} // namespace

std::filesystem::path packageCacheRoot(const std::filesystem::path &projectRoot) {
    return projectRoot /
           utf8Path(kPackageStateDirectory) /
           utf8Path(kPackageCacheDirectory);
}

std::filesystem::path packageCacheEntryPath(
    const std::filesystem::path &cacheRoot,
    const std::string &fingerprint) {
    return cacheRoot / utf8Path(cacheKeyForFingerprint(fingerprint));
}

std::filesystem::path cachePackageTree(
    const std::filesystem::path &installedPackage,
    const std::filesystem::path &cacheRoot,
    const std::string &fingerprint) {
    const std::filesystem::path entry =
        packageCacheEntryPath(cacheRoot, fingerprint);
    if (std::filesystem::exists(entry)) {
        if (fingerprintPackageTree(entry) != fingerprint) {
            throw std::runtime_error(
                "cache package bị thay đổi ngoài ý muốn: " + entry.u8string());
        }
        return entry;
    }
    std::filesystem::create_directories(cacheRoot);
    const std::filesystem::path staging =
        cacheRoot / utf8Path(".vpp-cache-stage-" + cacheKeyForFingerprint(fingerprint));
    std::error_code ignored;
    std::filesystem::remove_all(staging, ignored);
    try {
        copyExactTree(installedPackage, staging);
        const std::string stagedFingerprint = fingerprintPackageTree(staging);
        if (stagedFingerprint != fingerprint) {
            throw std::runtime_error(
                "không thể cache package: fingerprint thay đổi trong khi snapshot");
        }
        std::filesystem::rename(staging, entry);
    } catch (...) {
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
    return entry;
}

std::optional<std::filesystem::path> findCachedPackage(
    const std::filesystem::path &cacheRoot,
    const std::string &fingerprint) {
    const std::filesystem::path entry =
        packageCacheEntryPath(cacheRoot, fingerprint);
    if (!std::filesystem::exists(entry) || !std::filesystem::is_directory(entry)) {
        return std::nullopt;
    }
    if (fingerprintPackageTree(entry) != fingerprint) return std::nullopt;
    return entry;
}

} // namespace vietvm::core
