#pragma once

#include <string_view>

// Thông báo cho runtime, VM và các hàm native.
namespace vietvm::messages {

// VM value, bytecode and control-flow errors.
inline constexpr std::string_view kRuntimeValueNotNumeric = "toDouble: giá trị không phải số";
inline constexpr std::string_view kVmInvalidIntegerValue = "Lỗi: giá trị không phải số nguyên";
inline constexpr std::string_view kVmDivisionByZero = "Lỗi: chia cho 0";
inline constexpr std::string_view kVmInvalidFloatIndex = "Lỗi: chỉ số số thực không hợp lệ";
inline constexpr std::string_view kVmInvalidStringIndex = "Lỗi: chỉ số chuỗi không hợp lệ";
inline constexpr std::string_view kVmInvalidMapIndex = "Lỗi: chỉ số ánh xạ không hợp lệ";
inline constexpr std::string_view kVmInvalidListIndex = "Lỗi: chỉ số danh sách không hợp lệ";
inline constexpr std::string_view kVmIndexNeedsListOrString = "Lỗi: truy cập chỉ số chỉ áp dụng cho danh sách, bộ hoặc chuỗi";
inline constexpr std::string_view kVmIndexMustBeInteger = "Lỗi: chỉ số phải là số nguyên";
inline constexpr std::string_view kVmIndexOutOfRange = "Lỗi: chỉ số vượt phạm vi";
inline constexpr std::string_view kVmNotEnoughOperands = "Lỗi: không đủ toán hạng cho toán tử";
inline constexpr std::string_view kVmNumericOnlyOperator = "Lỗi: toán tử số chỉ áp dụng cho số";
inline constexpr std::string_view kVmLogicOnlyOperator = "Lỗi: toán tử logic chỉ áp dụng cho số";
inline constexpr std::string_view kVmCannotCompareDifferentTypes = "Lỗi: không thể so sánh hai kiểu dữ liệu khác nhau";
inline constexpr std::string_view kVmUnknownOperator = "Toán tử không xác định";
inline constexpr std::string_view kVmMissingModuloOperands = "Lỗi: thiếu toán hạng cho phép chia dư";
inline constexpr std::string_view kVmModuloByZero = "Lỗi: chia dư cho 0";
inline constexpr std::string_view kVmMissingNegationOperand = "Thiếu toán hạng cho toán tử phủ định !";
inline constexpr std::string_view kVmEmptyStackWhenPrint = "Lỗi: ngăn xếp rỗng khi thực hiện IN";
inline constexpr std::string_view kVmMissingNotOperands = "Lỗi: không đủ toán hạng cho toán tử phủ định";
inline constexpr std::string_view kVmUnknownOpcode = "Mã lệnh không xác định";
inline constexpr std::string_view kVmCannotConvertToFloat = "Lỗi: không thể chuyển '{0}' thành số thực";
inline constexpr std::string_view kVmParseMapLiteral = "Lỗi phân tích giá trị ánh xạ trực tiếp: {0}";
inline constexpr std::string_view kVmMapLiteralEncodeInvalid = "Dữ liệu mã hóa của giá trị ánh xạ trực tiếp không hợp lệ";
inline constexpr std::string_view kVmMapLiteralTypeTagInvalid = "Giá trị ánh xạ trực tiếp: thẻ kiểu không hợp lệ";
inline constexpr std::string_view kVmDefaultParamEncodeInvalid = "Dữ liệu mã hóa của tham số mặc định không hợp lệ";
inline constexpr std::string_view kVmDefaultParamTypeInvalid = "Kiểu của tham số mặc định không hợp lệ";
inline constexpr std::string_view kVmContinueMissingUpdate = "BO_QUA: không tìm thấy OP_CAP_NHAT trong vòng lặp";
inline constexpr std::string_view kVmSwitchEmptyStack = "CHON: ngăn xếp rỗng";
inline constexpr std::string_view kVmCaseOutsideSwitch = "CA: Không nằm trong khối CHON";
inline constexpr std::string_view kVmCaseInvalidOperandFormat = "CA: định dạng toán hạng không hợp lệ";
inline constexpr std::string_view kVmDefaultOutsideSwitch = "MAC_DINH: Không nằm trong khối CHON";
inline constexpr std::string_view kVmBreakOutsideSwitch = "THOAT: Không nằm trong khối CHON";
inline constexpr std::string_view kVmAssignNotEnoughOperands = "Không đủ toán hạng để GÁN";
inline constexpr std::string_view kVmVariableIdMustBeInt = "Lỗi: mã biến phải là số nguyên";
inline constexpr std::string_view kVmJumpAddressOutOfRange = "Lỗi: địa chỉ nhảy ngoài phạm vi";
inline constexpr std::string_view kVmJumpIfFalseEmptyStack = "Lỗi: ngăn xếp rỗng khi thực thi OP_JUMP_IF_FALSE";
inline constexpr std::string_view kVmJumpConditionMustBeInt = "Lỗi: điều kiện nhảy phải là số nguyên";
inline constexpr std::string_view kVmNoOpenBlock = "Lỗi: không có khối mở";
inline constexpr std::string_view kVmDefaultParamIndexInvalid = "OP_PARAM_MAC_DINH: chỉ số giá trị mặc định không hợp lệ";
inline constexpr std::string_view kVmIncrementEmptyStack = "OP_CONG_MOT: ngăn xếp rỗng";
inline constexpr std::string_view kVmIncrementCannotIncreaseString = "OP_CONG_MOT: không thể tăng giá trị chuỗi";
inline constexpr std::string_view kVmIncrementUnsupportedType = "OP_CONG_MOT: kiểu dữ liệu chưa được hỗ trợ";
inline constexpr std::string_view kVmDecrementEmptyStack = "OP_TRU_MOT: ngăn xếp rỗng";
inline constexpr std::string_view kVmDecrementUnsupportedType = "OP_TRU_MOT: kiểu dữ liệu chưa được hỗ trợ";
inline constexpr std::string_view kVmFunctionNotFound = "OP_GOI: hàm không tồn tại (mã/chỉ số tên={0}, tên='{1}')";
inline constexpr std::string_view kVmIndirectCallMissingReference = "OP_GOI_GIAN_TIEP: thiếu tham chiếu hàm";
inline constexpr std::string_view kVmIndirectCallInvalidReference = "OP_GOI_GIAN_TIEP: tham chiếu hàm không hợp lệ";
inline constexpr std::string_view kVmIndirectCallUnsupportedReferenceType = "OP_GOI_GIAN_TIEP: kiểu tham chiếu hàm chưa được hỗ trợ";
inline constexpr std::string_view kVmUnknownThrownValue = "lỗi không xác định";
inline constexpr std::string_view kVmUncaughtException = "Lỗi không bắt được: {0}";
inline constexpr std::string_view kVmParamArgIndexOutOfRange = "Cảnh báo: OP_PARAM có chỉ số đối số ngoài phạm vi, dùng mặc định 0";
inline constexpr std::string_view kVmOutputPrefix = "[IN] ";
inline constexpr std::string_view kVmLogPrefix = "[VM] ";

// Native-function invocation and file/time diagnostics.
inline constexpr std::string_view kNativeArgumentCount = "{0} yêu cầu {1} tham số";
inline constexpr std::string_view kNativeInvalidArgument = "{0}: {1} không hợp lệ";
inline constexpr std::string_view kNativeFileOpenForReadFailed = "{0}: không thể mở tệp để đọc";
inline constexpr std::string_view kNativeFileOpenForWriteFailed = "{0}: không thể mở tệp để ghi";
inline constexpr std::string_view kNativeFileWriteFailed = "{0}: ghi tệp thất bại";
inline constexpr std::string_view kNativeTimeFormatFailed = "lay_thoi_gian_hien_tai: định dạng thời gian thất bại";

// Native HTTP client/server diagnostics.
inline constexpr std::string_view kNativeHttpCurlProcessOpenFailed = "{0}: không mở được tiến trình curl";
inline constexpr std::string_view kNativeHttpCurlFailed = "{0}: curl trả về lỗi";
inline constexpr std::string_view kNativeHttpServerWsaStartupFailed = "mang_http_server_open: WSAStartup thất bại";
inline constexpr std::string_view kNativeHttpServerInvalidPort = "mang_http_server_open: cổng không hợp lệ";
inline constexpr std::string_view kNativeHttpServerSocketCreateFailed = "mang_http_server_open: không tạo được ổ cắm mạng (socket)";
inline constexpr std::string_view kNativeHttpServerExclusivePortFailed = "mang_http_server_open: không thể giữ riêng cổng (WSA={0})";
inline constexpr std::string_view kNativeHttpServerBindFailed = "mang_http_server_open: gắn địa chỉ/cổng (bind) thất bại (mã={0})";
inline constexpr std::string_view kNativeHttpServerListenFailed = "mang_http_server_open: lắng nghe kết nối (listen) thất bại";
inline constexpr std::string_view kNativeHttpServerNotFound = "mang_http_server_next: máy chủ không tồn tại";
inline constexpr std::string_view kNativeHttpRequestNotFound = "{0}: yêu cầu không tồn tại";
inline constexpr std::string_view kNativeHttpRequestFieldInvalid = "mang_http_req_field: trường yêu cầu không hợp lệ";
inline constexpr std::string_view kNativeHttpResponseSendFailed = "mang_http_server_send: gửi phản hồi thất bại";
inline constexpr std::string_view kNativeHttpServerListening = "[HTTP] máy chủ mức thấp đang lắng nghe tại 0.0.0.0:{0}";

// These are result payloads rather than thrown diagnostics.  Keep their text
// byte-for-byte compatible with programs that inspect DB_OK| and DB_ERR|.
inline constexpr std::string_view kNativeDbErrorResult = "DB_ERR|{0}";
inline constexpr std::string_view kNativeDbConnectedResult = "DB_OK|connected";
inline constexpr std::string_view kNativeDbAffectedOneResult = "DB_OK|affected=1";
inline constexpr std::string_view kNativeDbQueryResult = "DB_OK|{0}";


} // namespace vietvm::messages
