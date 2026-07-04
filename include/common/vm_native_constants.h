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
inline constexpr const char *kOutputPrefixIn = "[IN] ";

inline constexpr const char *kErrInvalidFloatIndex = "Lỗi: chỉ số float không hợp lệ";
inline constexpr const char *kErrInvalidStringIndex = "Lỗi: chỉ số chuỗi không hợp lệ";
inline constexpr const char *kErrNotEnoughOperands = "Lỗi: không đủ toán hạng cho toán tử";
inline constexpr const char *kErrNumericOnlyOperator = "Lỗi: toán tử số chỉ áp dụng cho số";
inline constexpr const char *kErrLogicOnlyOperator = "Lỗi: toán tử logic chỉ áp dụng cho số";
inline constexpr const char *kErrCannotCompareDifferentTypes = "Lỗi: không thể so sánh hai kiểu dữ liệu khác nhau";
inline constexpr const char *kErrUnknownOperator = "Toán tử không xác định";
inline constexpr const char *kErrMissingModuloOperands = "Lỗi: thiếu toán hạng cho MODULO";
inline constexpr const char *kErrModuloByZero = "Lỗi: chia dư cho 0";
inline constexpr const char *kErrMissingNegationOperand = "Thiếu toán hạng cho toán tử phủ định !";
inline constexpr const char *kErrEmptyStackWhenPrint = "Lỗi: stack rỗng khi IN";
inline constexpr const char *kErrJumpAddressOutOfRange = "Lỗi: địa chỉ nhảy ngoài phạm vi";
inline constexpr const char *kErrMissingNotOperands = "Lỗi: không đủ toán hạng cho toán tử phủ định";
inline constexpr const char *kErrCannotConvertToFloatPrefix = "Lỗi: không thể chuyển '";
inline constexpr const char *kErrCannotConvertToFloatSuffix = "' thành số thực";
inline constexpr const char *kErrInvalidMapIndex = "Lỗi: chỉ số map không hợp lệ";
inline constexpr const char *kErrParseMapLiteralPrefix = "Lỗi parse map literal: ";
inline constexpr const char *kErrContinueMissingUpdate = "BO_QUA: không tìm thấy OP_CAP_NHAT trong vòng lặp";
inline constexpr const char *kErrSwitchEmptyStack = "CHON: Stack rỗng";
inline constexpr const char *kErrCaseOutsideSwitch = "CA: Không nằm trong khối CHON";
inline constexpr const char *kErrCaseInvalidOperandFormat = "CA: định dạng operand không hợp lệ";
inline constexpr const char *kErrDefaultOutsideSwitch = "MAC_DINH: Không nằm trong khối CHON";
inline constexpr const char *kErrBreakOutsideSwitch = "THOAT: Không nằm trong khối CHON";
inline constexpr const char *kErrAssignNotEnoughOperands = "Không đủ toán hạng để GÁN";
inline constexpr const char *kErrVarIdMustBeInt = "Lỗi: ID biến phải là số nguyên";
inline constexpr const char *kErrJumpIfFalseEmptyStack = "Lỗi: Stack rỗng khi thực thi OP_JUMP_IF_FALSE";
inline constexpr const char *kErrJumpConditionMustBeInt = "Lỗi: điều kiện nhảy phải là số nguyên";
inline constexpr const char *kErrNoOpenBlock = "Lỗi: không có khối mở";
inline constexpr const char *kWarnParamArgIndexOutOfRange = "Cảnh báo: OP_PARAM argIndex ngoài phạm vi, gán mặc định 0";
inline constexpr const char *kErrDefaultParamIndexInvalid = "OP_PARAM_MAC_DINH: default index không hợp lệ";
inline constexpr const char *kErrIncEmptyStack = "OP_CONG_MOT: stack rong";
inline constexpr const char *kErrIncCannotIncreaseString = "OP_CONG_MOT: khong the tang chuoi";
inline constexpr const char *kErrIncUnsupportedType = "OP_CONG_MOT: kieu du lieu khong ho tro";
inline constexpr const char *kErrDecEmptyStack = "OP_TRU_MOT: stack rong";
inline constexpr const char *kErrDecUnsupportedType = "OP_TRU_MOT: kieu du lieu khong ho tro";
inline constexpr const char *kErrUnknownOpcode = "Opcode không xác định";
inline constexpr const char *kErrMapLiteralEncodeInvalid = "Map literal encode không hợp lệ";
inline constexpr const char *kErrMapLiteralTypeTagInvalid = "Map literal: typeTag không hợp lệ";
inline constexpr const char *kErrDefaultParamEncodeInvalid = "Default param encode không hợp lệ";
inline constexpr const char *kErrDefaultParamTypeInvalid = "Default param type không hợp lệ";
inline constexpr const char *kErrUnknownThrownValue = "lỗi không xác định";
inline constexpr const char *kErrUncaughtPrefix = "Lỗi không bắt được: ";
inline constexpr const char *kErrIndirectCallMissingRef = "OP_GOI_GIAN_TIEP: thiếu function reference";
inline constexpr const char *kErrIndirectCallInvalidRef = "OP_GOI_GIAN_TIEP: function reference không hợp lệ";
inline constexpr const char *kErrIndirectCallUnsupportedRefType = "OP_GOI_GIAN_TIEP: kiểu function reference không hỗ trợ";

} // namespace vietvm::constants