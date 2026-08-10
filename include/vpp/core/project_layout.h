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
inline constexpr const char *kPackageEntryFile = "main.vi";
inline constexpr const char *kPrimaryPackageDirectory = u8"gói";
inline constexpr const char *kBundledLibraryDirectory = u8"thư viện";
inline constexpr const char *kBundledLibraryMainFile = u8"gói/thư viện/main.vi";
inline constexpr const char *kEnvVppHome = "VPP_HOME";

inline constexpr std::array<const char *, 3> kPackageDirectoryNames = {
    kPrimaryPackageDirectory,
    "goi",
    "packages",
};

inline std::filesystem::path utf8Path(const char *value) {
    return std::filesystem::u8path(value);
}

inline std::filesystem::path utf8Path(const std::string &value) {
    return std::filesystem::u8path(value);
}

inline std::filesystem::path packageEntryPath(const std::filesystem::path &packageDirectory) {
    return packageDirectory / utf8Path(kPackageEntryFile);
}

inline bool isPackageDirectoryName(const std::string &name) {
    for (const char *candidate : kPackageDirectoryNames) {
        if (name == candidate) return true;
    }
    return false;
}

} // namespace vietvm::core
