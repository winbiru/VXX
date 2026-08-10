#pragma once

namespace vietvm::messages {

// VM/native diagnostics: VPP-VM and VPP-NATIVE.
//
// This header is included through `message_constants.h`, which declares
// MessageDefinition, formatMessage and messageText.

// VM value, bytecode and control-flow errors.
inline constexpr MessageDefinition kVmInvalidIntegerValue{
    "VPP-VM-TYPE-001", "Lỗi: giá trị không phải số nguyên"};
inline constexpr MessageDefinition kVmDivisionByZero{
    "VPP-VM-ARITH-001", "Lỗi: chia cho 0"};
inline constexpr MessageDefinition kVmInvalidFloatIndex{
    "VPP-VM-INDEX-001", "Lỗi: chỉ số float không hợp lệ"};
inline constexpr MessageDefinition kVmInvalidStringIndex{
    "VPP-VM-INDEX-002", "Lỗi: chỉ số chuỗi không hợp lệ"};
inline constexpr MessageDefinition kVmInvalidMapIndex{
    "VPP-VM-INDEX-003", "Lỗi: chỉ số map không hợp lệ"};
inline constexpr MessageDefinition kVmNotEnoughOperands{
    "VPP-VM-OP-001", "Lỗi: không đủ toán hạng cho toán tử"};
inline constexpr MessageDefinition kVmNumericOnlyOperator{
    "VPP-VM-OP-002", "Lỗi: toán tử số chỉ áp dụng cho số"};
inline constexpr MessageDefinition kVmLogicOnlyOperator{
    "VPP-VM-OP-003", "Lỗi: toán tử logic chỉ áp dụng cho số"};
inline constexpr MessageDefinition kVmCannotCompareDifferentTypes{
    "VPP-VM-OP-004", "Lỗi: không thể so sánh hai kiểu dữ liệu khác nhau"};
inline constexpr MessageDefinition kVmUnknownOperator{
    "VPP-VM-OP-005", "Toán tử không xác định"};
inline constexpr MessageDefinition kVmMissingModuloOperands{
    "VPP-VM-OP-006", "Lỗi: thiếu toán hạng cho MODULO"};
inline constexpr MessageDefinition kVmModuloByZero{
    "VPP-VM-OP-007", "Lỗi: chia dư cho 0"};
inline constexpr MessageDefinition kVmMissingNegationOperand{
    "VPP-VM-OP-008", "Thiếu toán hạng cho toán tử phủ định !"};
inline constexpr MessageDefinition kVmEmptyStackWhenPrint{
    "VPP-VM-OP-009", "Lỗi: stack rỗng khi IN"};
inline constexpr MessageDefinition kVmMissingNotOperands{
    "VPP-VM-OP-010", "Lỗi: không đủ toán hạng cho toán tử phủ định"};
inline constexpr MessageDefinition kVmUnknownOpcode{
    "VPP-VM-OP-011", "Opcode không xác định"};
inline constexpr MessageDefinition kVmCannotConvertToFloat{
    "VPP-VM-TYPE-002", "Lỗi: không thể chuyển '{0}' thành số thực"};
inline constexpr MessageDefinition kVmParseMapLiteral{
    "VPP-VM-ENCODE-001", "Lỗi parse map literal: {0}"};
inline constexpr MessageDefinition kVmMapLiteralEncodeInvalid{
    "VPP-VM-ENCODE-002", "Map literal encode không hợp lệ"};
inline constexpr MessageDefinition kVmMapLiteralTypeTagInvalid{
    "VPP-VM-ENCODE-003", "Map literal: typeTag không hợp lệ"};
inline constexpr MessageDefinition kVmDefaultParamEncodeInvalid{
    "VPP-VM-ENCODE-004", "Default param encode không hợp lệ"};
inline constexpr MessageDefinition kVmDefaultParamTypeInvalid{
    "VPP-VM-ENCODE-005", "Default param type không hợp lệ"};
inline constexpr MessageDefinition kVmContinueMissingUpdate{
    "VPP-VM-CONTROL-001", "BO_QUA: không tìm thấy OP_CAP_NHAT trong vòng lặp"};
inline constexpr MessageDefinition kVmSwitchEmptyStack{
    "VPP-VM-CONTROL-002", "CHON: Stack rỗng"};
inline constexpr MessageDefinition kVmCaseOutsideSwitch{
    "VPP-VM-CONTROL-003", "CA: Không nằm trong khối CHON"};
inline constexpr MessageDefinition kVmCaseInvalidOperandFormat{
    "VPP-VM-CONTROL-004", "CA: định dạng operand không hợp lệ"};
inline constexpr MessageDefinition kVmDefaultOutsideSwitch{
    "VPP-VM-CONTROL-005", "MAC_DINH: Không nằm trong khối CHON"};
inline constexpr MessageDefinition kVmBreakOutsideSwitch{
    "VPP-VM-CONTROL-006", "THOAT: Không nằm trong khối CHON"};
inline constexpr MessageDefinition kVmAssignNotEnoughOperands{
    "VPP-VM-CONTROL-007", "Không đủ toán hạng để GÁN"};
inline constexpr MessageDefinition kVmVariableIdMustBeInt{
    "VPP-VM-CONTROL-008", "Lỗi: ID biến phải là số nguyên"};
inline constexpr MessageDefinition kVmJumpAddressOutOfRange{
    "VPP-VM-CONTROL-009", "Lỗi: địa chỉ nhảy ngoài phạm vi"};
inline constexpr MessageDefinition kVmJumpIfFalseEmptyStack{
    "VPP-VM-CONTROL-010", "Lỗi: Stack rỗng khi thực thi OP_JUMP_IF_FALSE"};
inline constexpr MessageDefinition kVmJumpConditionMustBeInt{
    "VPP-VM-CONTROL-011", "Lỗi: điều kiện nhảy phải là số nguyên"};
inline constexpr MessageDefinition kVmNoOpenBlock{
    "VPP-VM-CONTROL-012", "Lỗi: không có khối mở"};
inline constexpr MessageDefinition kVmDefaultParamIndexInvalid{
    "VPP-VM-CONTROL-013", "OP_PARAM_MAC_DINH: default index không hợp lệ"};
inline constexpr MessageDefinition kVmIncrementEmptyStack{
    "VPP-VM-CONTROL-014", "OP_CONG_MOT: stack rong"};
inline constexpr MessageDefinition kVmIncrementCannotIncreaseString{
    "VPP-VM-CONTROL-015", "OP_CONG_MOT: khong the tang chuoi"};
inline constexpr MessageDefinition kVmIncrementUnsupportedType{
    "VPP-VM-CONTROL-016", "OP_CONG_MOT: kieu du lieu khong ho tro"};
inline constexpr MessageDefinition kVmDecrementEmptyStack{
    "VPP-VM-CONTROL-017", "OP_TRU_MOT: stack rong"};
inline constexpr MessageDefinition kVmDecrementUnsupportedType{
    "VPP-VM-CONTROL-018", "OP_TRU_MOT: kieu du lieu khong ho tro"};
inline constexpr MessageDefinition kVmFunctionNotFound{
    "VPP-VM-CALL-001", "OP_GOI: hàm không tồn tại (id/nameIndex={0}, name='{1}')"};
inline constexpr MessageDefinition kVmIndirectCallMissingReference{
    "VPP-VM-CALL-002", "OP_GOI_GIAN_TIEP: thiếu function reference"};
inline constexpr MessageDefinition kVmIndirectCallInvalidReference{
    "VPP-VM-CALL-003", "OP_GOI_GIAN_TIEP: function reference không hợp lệ"};
inline constexpr MessageDefinition kVmIndirectCallUnsupportedReferenceType{
    "VPP-VM-CALL-004", "OP_GOI_GIAN_TIEP: kiểu function reference không hỗ trợ"};
inline constexpr MessageDefinition kVmUnknownThrownValue{
    "VPP-VM-EXCEPTION-001", "lỗi không xác định"};
inline constexpr MessageDefinition kVmUncaughtException{
    "VPP-VM-EXCEPTION-002", "Lỗi không bắt được: {0}"};
inline constexpr MessageDefinition kVmParamArgIndexOutOfRange{
    "VPP-VM-WARN-001", "Cảnh báo: OP_PARAM argIndex ngoài phạm vi, gán mặc định 0"};
inline constexpr MessageDefinition kVmOutputPrefix{
    "VPP-VM-INFO-001", "[IN] "};
inline constexpr MessageDefinition kVmLogPrefix{
    "VPP-VM-INFO-002", "[VM] "};

// Native-function invocation and file/time diagnostics.
inline constexpr MessageDefinition kNativeArgumentCount{
    "VPP-NATIVE-ARG-001", "{0} yêu cầu {1} tham số"};
inline constexpr MessageDefinition kNativeInvalidArgument{
    "VPP-NATIVE-ARG-002", "{0}: {1} không hợp lệ"};
inline constexpr MessageDefinition kNativeFileOpenForReadFailed{
    "VPP-NATIVE-IO-001", "{0}: không thể mở file"};
inline constexpr MessageDefinition kNativeFileOpenForWriteFailed{
    "VPP-NATIVE-IO-002", "{0}: không thể mở file để ghi"};
inline constexpr MessageDefinition kNativeFileWriteFailed{
    "VPP-NATIVE-IO-003", "{0}: ghi file thất bại"};
inline constexpr MessageDefinition kNativeTimeFormatFailed{
    "VPP-NATIVE-TIME-001", "lay_thoi_gian_hien_tai: format thời gian thất bại"};

// Native HTTP client/server diagnostics.
inline constexpr MessageDefinition kNativeHttpCurlProcessOpenFailed{
    "VPP-NATIVE-HTTP-CLIENT-001", "{0}: không mở được tiến trình curl"};
inline constexpr MessageDefinition kNativeHttpCurlFailed{
    "VPP-NATIVE-HTTP-CLIENT-002", "{0}: curl trả về lỗi"};
inline constexpr MessageDefinition kNativeHttpServerWsaStartupFailed{
    "VPP-NATIVE-HTTP-SERVER-001", "mang_http_server_open: WSAStartup thất bại"};
inline constexpr MessageDefinition kNativeHttpServerInvalidPort{
    "VPP-NATIVE-HTTP-SERVER-002", "mang_http_server_open: cổng không hợp lệ"};
inline constexpr MessageDefinition kNativeHttpServerSocketCreateFailed{
    "VPP-NATIVE-HTTP-SERVER-003", "mang_http_server_open: không tạo được socket"};
inline constexpr MessageDefinition kNativeHttpServerExclusivePortFailed{
    "VPP-NATIVE-HTTP-SERVER-004", "mang_http_server_open: không thể giữ riêng cổng (WSA={0})"};
inline constexpr MessageDefinition kNativeHttpServerBindFailed{
    "VPP-NATIVE-HTTP-SERVER-005", "mang_http_server_open: bind thất bại"};
inline constexpr MessageDefinition kNativeHttpServerListenFailed{
    "VPP-NATIVE-HTTP-SERVER-006", "mang_http_server_open: listen thất bại"};
inline constexpr MessageDefinition kNativeHttpServerNotFound{
    "VPP-NATIVE-HTTP-SERVER-007", "mang_http_server_next: server không tồn tại"};
inline constexpr MessageDefinition kNativeHttpRequestNotFound{
    "VPP-NATIVE-HTTP-REQUEST-001", "{0}: request không tồn tại"};
inline constexpr MessageDefinition kNativeHttpRequestFieldInvalid{
    "VPP-NATIVE-HTTP-REQUEST-002", "mang_http_req_field: field không hợp lệ"};
inline constexpr MessageDefinition kNativeHttpResponseSendFailed{
    "VPP-NATIVE-HTTP-REQUEST-003", "mang_http_server_send: gửi phản hồi thất bại"};
inline constexpr MessageDefinition kNativeHttpServerListening{
    "VPP-NATIVE-HTTP-INFO-001", "[HTTP] low-level server listening on 0.0.0.0:{0}"};

// These are result payloads rather than thrown diagnostics.  Keep their text
// byte-for-byte compatible with programs that inspect DB_OK| and DB_ERR|.
inline constexpr MessageDefinition kNativeDbErrorResult{
    "VPP-NATIVE-DB-RESULT-001", "DB_ERR|{0}"};
inline constexpr MessageDefinition kNativeDbConnectedResult{
    "VPP-NATIVE-DB-RESULT-002", "DB_OK|connected"};
inline constexpr MessageDefinition kNativeDbAffectedOneResult{
    "VPP-NATIVE-DB-RESULT-003", "DB_OK|affected=1"};
inline constexpr MessageDefinition kNativeDbQueryResult{
    "VPP-NATIVE-DB-RESULT-004", "DB_OK|{0}"};

} // namespace vietvm::messages
