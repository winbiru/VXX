#pragma once

#include <array>
#include <string>

namespace vietvm::constants {

// Trả tên văn bản ổn định cho matches any; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
template <size_t N>
inline bool matchesAnyName(const std::string &fn, const std::array<const char *, N> &names) {
    for (const char *name : names) {
        if (fn == name) return true;
    }
    return false;
}

inline constexpr std::array<const char *, 2> kFnTypeOf = {
    "loai_cua", "loại của"
};

inline constexpr std::array<const char *, 1> kFnIdentityHash = {
    "bam_dinh_danh"
};

inline constexpr std::array<const char *, 2> kFnLength = {
    "do_dai", "độ dài"
};

inline constexpr std::array<const char *, 2> kFnListAppend = {
    "them", "thêm"
};

inline constexpr std::array<const char *, 2> kFnListRemoveAt = {
    "xoa_tai", "xóa tại"
};

inline constexpr std::array<const char *, 2> kFnMapHasKey = {
    "co_khoa", "có khóa"
};

inline constexpr std::array<const char *, 2> kFnMapRemove = {
    "xoa_khoa", "xóa khóa"
};

inline constexpr std::array<const char *, 2> kFnMapKeys = {
    "khoa_map", "khóa map"
};

inline constexpr int kHttpStatusOk = 200;

inline constexpr const char *kArgLabelPort = "cổng";
inline constexpr const char *kArgLabelServerId = "mã máy chủ";
inline constexpr const char *kArgLabelStatus = "trạng thái";

inline constexpr const char *kReqFieldMethod = "method";
inline constexpr const char *kReqFieldPath = "path";
inline constexpr const char *kReqFieldQuery = "query";
inline constexpr const char *kReqFieldBody = "body";
inline constexpr const char *kReqFieldHeader = "header";
inline constexpr const char *kEnvVppEnableGc = "VPP_ENABLE_GC";
inline constexpr const char *kEnvVietvmEnableGc = "VIETVM_ENABLE_GC";
inline constexpr const char *kEnvVppGcInterval = "VPP_GC_INTERVAL";
inline constexpr const char *kEnvVietvmGcInterval = "VIETVM_GC_INTERVAL";
inline constexpr const char *kEnvVppEnableJit = "VPP_ENABLE_JIT";
inline constexpr const char *kEnvVietvmEnableJit = "VIETVM_ENABLE_JIT";

inline constexpr int kDefaultGcInterval = 2048;

} // namespace vietvm::constants
