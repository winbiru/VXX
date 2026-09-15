#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace vietvm::core {

// Shared project/package layout.  These strings are UTF-8 where Vietnamese
// names are involved, so callers should construct filesystem paths with
// utf8Path() rather than fs::path(const char*).
inline constexpr const char *kCliVersion = "0.1.0";
inline constexpr const char *kProjectManifestFile = "vpp.json";
inline constexpr const char *kPackageLockFile = "vpp.lock";
inline constexpr const char *kPackageStateDirectory = ".vpp";
inline constexpr const char *kPackageCacheDirectory = "cache";
inline constexpr const char *kPackageEntryFile = "main.vi";
inline constexpr const char *kRegistryMarkerFile = "vpp-registry.json";
inline constexpr const char *kPrimaryPackageDirectory = u8"gói";
// `gói/chuẩn` là package tổng hợp. Các package chuẩn (`lõi`, `mạng`, ...)
// nằm trực tiếp dưới `gói/` để đúng layout `gói/<tên>/main.vi`.
inline constexpr const char *kStandardPackageDirectory = u8"chuẩn";
inline constexpr const char *kStandardPackageMainFile = u8"gói/chuẩn/main.vi";
inline constexpr const char *kEnvVppHome = "VPP_HOME";
inline constexpr const char *kEnvVppRegistry = "VPP_REGISTRY";

inline constexpr std::array<const char *, 3> kPackageDirectoryNames = {
    kPrimaryPackageDirectory,
    "goi",
    "packages",
};

// Chuyển chuỗi UTF-8 thành `std::filesystem::path` theo quy tắc của nền tảng để đường dẫn Unicode hoạt động nhất quán.
inline std::filesystem::path utf8Path(const char *value) {
    return std::filesystem::u8path(value);
}

// Chuyển chuỗi UTF-8 thành `std::filesystem::path` theo quy tắc của nền tảng để đường dẫn Unicode hoạt động nhất quán.
inline std::filesystem::path utf8Path(const std::string &value) {
    return std::filesystem::u8path(value);
}

// Tạo đường dẫn entry point của package từ thư mục gốc/manifest; hàm ghép các thành phần layout theo quy ước dự án V++.
inline std::filesystem::path packageEntryPath(const std::filesystem::path &packageDirectory) {
    return packageDirectory / utf8Path(kPackageEntryFile);
}

// Kiểm tra điều kiện của `isPackageDirectoryName`.
inline bool isPackageDirectoryName(const std::string &name) {
    for (const char *candidate : kPackageDirectoryNames) {
        if (name == candidate) return true;
    }
    return false;
}

} // namespace vietvm::core
