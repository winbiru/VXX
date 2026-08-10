#pragma once

#include <array>
#include <string>

namespace vietvm::constants {

template <size_t N>
inline bool matchesAnyName(const std::string &fn, const std::array<const char *, N> &names) {
    for (const char *name : names) {
        if (fn == name) return true;
    }
    return false;
}

inline constexpr std::array<const char *, 3> kFnHttpGet = {
    "mang_http_get", "mạnglấy", "mạng lấy"
};

inline constexpr std::array<const char *, 3> kFnHttpPost = {
    "mang_http_post", "mạnggửi", "mạng gửi"
};

inline constexpr std::array<const char *, 3> kFnHttpPut = {
    "mang_http_put", "mạngcậpnhật", "mạng cập nhật"
};

inline constexpr std::array<const char *, 3> kFnHttpDelete = {
    "mang_http_delete", "mạngxóa", "mạng xóa"
};

inline constexpr std::array<const char *, 3> kFnJsonEscape = {
    "json_escape", "jsonthoát", "json thoát"
};

inline constexpr std::array<const char *, 3> kFnJsonString = {
    "json_quote", "jsonchuỗi", "json chuỗi"
};

inline constexpr std::array<const char *, 3> kFnHttpServerOpen = {
    "mang_http_server_open", "mạngmởmáychủapi", "mạng mở máy chủ api"
};

inline constexpr std::array<const char *, 3> kFnHttpServerNext = {
    "mang_http_server_next", "mạnglấyyêucầukếtiếp", "mạng lấy yêu cầu kế tiếp"
};

inline constexpr std::array<const char *, 3> kFnHttpReqMethod = {
    "mang_http_req_method", "mạngmethodyêucầu", "mạng method yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqPath = {
    "mang_http_req_path", "mạngpathyêucầu", "mạng path yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqQuery = {
    "mang_http_req_query", "mạngqueryyêucầu", "mạng query yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqBody = {
    "mang_http_req_body", "mạngbodyyêucầu", "mạng body yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqHeader = {
    "mang_http_req_header", "mạngheaderyêucầu", "mạng header yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqQueryParam = {
    "mang_http_req_query_param", "mạngqueryparamyêucầu", "mạng query param yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqJsonField = {
    "mang_http_req_json_field", "mạngjsonfieldyêucầu", "mạng json field yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpReqPathSuffix = {
    "mang_http_req_path_suffix", "mạngpathsuffixyêucầu", "mạng path suffix yêu cầu"
};

inline constexpr std::array<const char *, 3> kFnHttpServerSend = {
    "mang_http_server_send", "mạngtrảphảnhồi", "mạng trả phản hồi"
};

inline constexpr std::array<const char *, 3> kFnHttpServerClose = {
    "mang_http_server_close", "mạngđóngmáychủapi", "mạng đóng máy chủ api"
};

inline constexpr std::array<const char *, 3> kFnIoReadFile = {
    "io_doc_file", "đọctệp", "đọc tệp"
};

inline constexpr std::array<const char *, 3> kFnIoWriteFile = {
    "io_ghi_file", "ghitọệp", "ghi tệp"
};

inline constexpr std::array<const char *, 3> kFnNow = {
    "lay_thoi_gian_hien_tai", "lấythờigianhiệntại", "lấy thời gian hiện tại"
};

inline constexpr std::array<const char *, 3> kFnReadConfig = {
    "doc_config", "đọccấuhình", "đọc cấu hình"
};

inline constexpr std::array<const char *, 3> kFnReadConfigKey = {
    "doc_config_key", "đọccấuhìnhkhóa", "đọc cấu hình khóa"
};

inline constexpr std::array<const char *, 1> kFnDbConnect = {
    "db_native_connect"
};

inline constexpr std::array<const char *, 1> kFnDbQuery = {
    "db_native_query"
};

inline constexpr const char *kHttpMethodGet = "GET";
inline constexpr const char *kHttpMethodPost = "POST";
inline constexpr const char *kHttpMethodPut = "PUT";
inline constexpr const char *kHttpMethodDelete = "DELETE";

inline constexpr int kHttpStatusOk = 200;

inline constexpr const char *kArgLabelPort = "cổng";
inline constexpr const char *kArgLabelServerId = "server id";
inline constexpr const char *kArgLabelStatus = "status";

inline constexpr const char *kReqFieldMethod = "method";
inline constexpr const char *kReqFieldPath = "path";
inline constexpr const char *kReqFieldQuery = "query";
inline constexpr const char *kReqFieldBody = "body";
inline constexpr const char *kReqFieldHeader = "header";
inline constexpr const char *kReqFieldQueryParam = "query_param";
inline constexpr const char *kReqFieldJsonField = "json_field";
inline constexpr const char *kReqFieldPathSuffix = "path_suffix";

inline constexpr const char *kEnvVppEnableGc = "VPP_ENABLE_GC";
inline constexpr const char *kEnvVietvmEnableGc = "VIETVM_ENABLE_GC";
inline constexpr const char *kEnvVppGcInterval = "VPP_GC_INTERVAL";
inline constexpr const char *kEnvVietvmGcInterval = "VIETVM_GC_INTERVAL";
inline constexpr const char *kEnvVppEnableJit = "VPP_ENABLE_JIT";
inline constexpr const char *kEnvVietvmEnableJit = "VIETVM_ENABLE_JIT";

inline constexpr int kDefaultGcInterval = 2048;

// DB result reasons are part of the native-function protocol.  Their text is
// intentionally stable because V++ programs compare the `DB_ERR|...` value.
inline constexpr const char *kDbReasonCommandFailed = "db-command-failed";
inline constexpr const char *kDbReasonInvalidJdbcPrefix = "invalid-jdbc-prefix";
inline constexpr const char *kDbReasonInvalidHostOrDatabase = "invalid-host-or-database";
inline constexpr const char *kDbReasonInvalidPort = "invalid-port";
inline constexpr const char *kDbReasonMissingHost = "missing-host";
inline constexpr const char *kDbReasonMissingDatabase = "missing-database";
inline constexpr const char *kDbReasonMissingSqlitePath = "missing-sqlite-path";
inline constexpr const char *kDbReasonUnsupportedDriver = "unsupported-driver";
inline constexpr const char *kDbReasonMissingUsername = "missing-username";
inline constexpr const char *kDbReasonCannotOpenMysqlProcess = "cannot-open-mysql-process";
inline constexpr const char *kDbReasonCannotOpenPsqlProcess = "cannot-open-psql-process";
inline constexpr const char *kDbReasonCannotOpenSqliteProcess = "cannot-open-sqlite-process";
inline constexpr const char *kDbReasonUnsafeDatabaseName = "unsafe-database-name";

} // namespace vietvm::constants
