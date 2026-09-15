#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vietvm::core {

std::filesystem::path packageCacheRoot(const std::filesystem::path &projectRoot);
std::filesystem::path packageCacheEntryPath(
    const std::filesystem::path &cacheRoot,
    const std::string &fingerprint);

// Snapshot một installed package vào content-addressed cache. Nếu entry đã có,
// fingerprint được kiểm tra lại trước khi tái sử dụng.
std::filesystem::path cachePackageTree(
    const std::filesystem::path &installedPackage,
    const std::filesystem::path &cacheRoot,
    const std::string &fingerprint);

// Trả cache entry chỉ khi bytes hiện tại vẫn khớp fingerprint mong đợi.
std::optional<std::filesystem::path> findCachedPackage(
    const std::filesystem::path &cacheRoot,
    const std::string &fingerprint);

} // namespace vietvm::core
