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

inline constexpr std::array<const char *, 1> kFnIoReadFile = {
    "io_doc_file"
};

inline constexpr std::array<const char *, 1> kFnIoWriteFile = {
    "io_ghi_file"
};

inline constexpr std::array<const char *, 1> kFnIoAppendFile = {
    "io_ghi_tiep_file"
};

inline constexpr std::array<const char *, 1> kFnIoReadBytes = {
    "io_doc_bytes"
};

inline constexpr std::array<const char *, 1> kFnIoWriteBytes = {
    "io_ghi_bytes"
};

inline constexpr std::array<const char *, 1> kFnWallClockParts = {
    "dong_ho_lich_noi_bo"
};

inline constexpr std::array<const char *, 1> kFnMonotonicMilliseconds = {
    "thoi_gian_don_dieu_mili_giay"
};

inline constexpr std::array<const char *, 1> kFnSpecialFloatInternal = {
    "so_thuc_dac_biet_noi_bo"
};

inline constexpr std::array<const char *, 2> kFnTypeOf = {
    "loai_cua", "loại của"
};

inline constexpr std::array<const char *, 1> kFnIdentityHash = {
    "bam_dinh_danh"
};

inline constexpr std::array<const char *, 1> kFnFormatFloatInternal = {
    "dinh_dang_so_thuc_noi_bo"
};

inline constexpr std::array<const char *, 1> kFnSecureRandom = {
    "ngau_nhien_bao_mat_bytes"
};

inline constexpr std::array<const char *, 1> kFnPathExists = {
    "duong_dan_ton_tai"
};

inline constexpr std::array<const char *, 1> kFnPathIsFile = {
    "la_tep"
};

inline constexpr std::array<const char *, 1> kFnPathIsDirectory = {
    "la_thu_muc"
};

inline constexpr std::array<const char *, 1> kFnCreateDirectory = {
    "tao_thu_muc"
};

inline constexpr std::array<const char *, 1> kFnListDirectory = {
    "liet_ke_thu_muc"
};

inline constexpr std::array<const char *, 1> kFnRemovePath = {
    "xoa_duong_dan"
};

inline constexpr std::array<const char *, 1> kFnEnvGet = {
    "doc_bien_moi_truong"
};

inline constexpr std::array<const char *, 1> kFnPlatformName = {
    "ten_nen_tang"
};

inline constexpr std::array<const char *, 1> kFnSleepMs = {
    "ngu_mili_giay"
};

inline constexpr std::array<const char *, 1> kFnDbExec = {
    "db_native_exec"
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

inline constexpr std::array<const char *, 2> kFnMapGet = {
    "lay_map", "lấy map"
};

inline constexpr std::array<const char *, 2> kFnMapSet = {
    "dat_map", "đặt map"
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

inline constexpr std::array<const char *, 1> kFnToTuple = {
    "thanh_tuple"
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

// DB result reasons are part of the native-function protocol.  Their text is
// intentionally stable because V++ programs compare the `DB_ERR|...` value.
inline constexpr const char *kDbReasonUnsupportedDriver = "unsupported-driver";
inline constexpr const char *kDbReasonMissingUsername = "missing-username";
inline constexpr const char *kDbReasonCannotOpenMysqlProcess = "cannot-open-mysql-process";
inline constexpr const char *kDbReasonCannotOpenPsqlProcess = "cannot-open-psql-process";
inline constexpr const char *kDbReasonCannotOpenSqliteProcess = "cannot-open-sqlite-process";

} // namespace vietvm::constants
