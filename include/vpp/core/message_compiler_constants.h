#pragma once

#include <string_view>

// Thông báo cho lexer, parser, semantic, import và compiler nội bộ.
namespace vietvm::messages {

// Lexer diagnostics.
inline constexpr std::string_view kLexerUnclosedString = "Chuỗi không được đóng (thiếu dấu nháy kết thúc)";
inline constexpr std::string_view kLexerUnclosedComment = "Khối chú thích không được đóng (thiếu */)";
inline constexpr std::string_view kLexerInvalidCharacter = "Ký tự không hợp lệ: '{0}' (mã: {1})";
inline constexpr std::string_view kLexerInvalidEscapeSequence = "Chuỗi thoát không hợp lệ: \\{0}";
inline constexpr std::string_view kLexerUnexpectedToken = "Từ tố không mong đợi: '{0}'";
inline constexpr std::string_view kLexerUnexpectedTokenWithExpectation = "Từ tố không mong đợi: '{0}', mong đợi: {1}";
inline constexpr std::string_view kLexerInvalidNumber = "Số không hợp lệ: '{0}'";
inline constexpr std::string_view kLexerInvalidIdentifier = "Tên định danh không hợp lệ: '{0}'";
inline constexpr std::string_view kLexerEmptyIdentifier = "Tên định danh không được rỗng";
inline constexpr std::string_view kLexerInvalidString = "Chuỗi không hợp lệ: '{0}'";
inline constexpr std::string_view kLexerMalformedString = "Chuỗi không được đóng đúng cách: '{0}'";
inline constexpr std::string_view kLexerMissingVietnameseAccent = "Từ khóa phải có dấu tiếng Việt: '{0}'";

// Legacy integer-variable helper diagnostics.  The helper lives in lexer.cpp
// for compatibility, but its failures are compiler-facing rather than lexical.
inline constexpr std::string_view kCompilerVariableIdOutOfRange = "getVarValueInt: mã biến ngoài phạm vi (id={0}, kích thước={1})";
inline constexpr std::string_view kCompilerVariableNotInteger = "getVarValueInt: giá trị biến không phải số hợp lệ: '{0}'";
inline constexpr std::string_view kCompilerVariableIntegerOverflow = "getVarValueInt: giá trị số quá lớn: '{0}'";
inline constexpr std::string_view kCompilerUnsupportedVariableValue = "getVarValueInt: kiểu giá trị không hỗ trợ";

// Source syntax diagnostics.
inline constexpr std::string_view kSyntaxExpectedOpeningParen = "extractParens: thiếu dấu '(' mở";
inline constexpr std::string_view kSyntaxUnbalancedParens = "extractParens: cặp ngoặc tròn không cân bằng";
inline constexpr std::string_view kSyntaxExpectedOpeningBlock = "extractBlock: thiếu dấu '{' mở";
inline constexpr std::string_view kSyntaxUnbalancedBraces = "extractBlock: cặp ngoặc nhọn không cân bằng";
inline constexpr std::string_view kSyntaxMissingAssignmentOperator = "extractAssignedVar: không tìm thấy dấu '='";
inline constexpr std::string_view kSyntaxInvalidLoopParts = "compileLoop: không thể phân tích các thành phần của vòng lặp";
inline constexpr std::string_view kSyntaxElseWithoutIf = "'hoặc' phải đi ngay sau một khối 'nếu'";
inline constexpr std::string_view kSyntaxMissingFunctionName = "compile: thiếu tên hàm sau 'hàm'";
inline constexpr std::string_view kSyntaxInvalidFunctionName = "compile: tên hàm không hợp lệ sau 'hàm'";
inline constexpr std::string_view kSyntaxExpectedBlockAtPosition = "compileBlock: cần dấu '{' tại vị trí {0}, nhưng gặp từ tố '{1}'";
inline constexpr std::string_view kSyntaxModifierBeforeFunction = "Dùng cú pháp 'hàm <quyền>' (ví dụ: 'hàm {0} tenHam(...)') thay vì '<quyền> hàm'";
inline constexpr std::string_view kSyntaxMissingClassName = "lớp: thiếu tên lớp";
inline constexpr std::string_view kSyntaxMissingClassOpeningBlock = "lớp: thiếu '{' sau tên lớp";
inline constexpr std::string_view kSyntaxUnsupportedClassMember = "lớp: hiện chỉ hỗ trợ khai báo hàm trong thân lớp";
inline constexpr std::string_view kSyntaxMissingClassClosingBlock = "lớp: thiếu '}' kết thúc lớp";
inline constexpr std::string_view kSyntaxMissingCallName = "gọi: thiếu tên hàm";
inline constexpr std::string_view kSyntaxInvalidCallName = "gọi: tên hàm không hợp lệ";
inline constexpr std::string_view kSyntaxMissingCallOpeningParen = "gọi: thiếu '(' sau tên hàm";
inline constexpr std::string_view kSyntaxReturnMustUseVe = "'trả' phải đi cùng 'về'";
inline constexpr std::string_view kSyntaxSwitchWrongEntryToken = "compileSwitch: từ tố tại vị trí hiện tại không phải 'chọn'";
inline constexpr std::string_view kSyntaxSwitchMissingOpeningBlock = "compileSwitch: thiếu dấu '{'";
inline constexpr std::string_view kSyntaxSwitchMissingCaseExpression = "compileSwitch: thiếu biểu thức sau 'ca'";
inline constexpr std::string_view kSyntaxSwitchInvalidToken = "Từ tố không hợp lệ trong khối chọn: '{0}' (dạng chuẩn='{1}')";
inline constexpr std::string_view kSyntaxSwitchMissingClosingBlock = "compileSwitch: thiếu dấu '}'";
inline constexpr std::string_view kSyntaxInvalidMapLiteral = "Giá trị ánh xạ trực tiếp không hợp lệ";
inline constexpr std::string_view kSyntaxInvalidMapKey = "Giá trị ánh xạ trực tiếp: khóa phải là chuỗi hoặc tên định danh";
inline constexpr std::string_view kSyntaxMapMissingColon = "Giá trị ánh xạ trực tiếp: thiếu dấu ':' sau khóa";
inline constexpr std::string_view kSyntaxMapMissingValue = "Giá trị ánh xạ trực tiếp: thiếu giá trị";
inline constexpr std::string_view kSyntaxUnsupportedMapValue = "Giá trị ánh xạ trực tiếp chỉ hỗ trợ số nguyên, số thực, chuỗi, đúng, sai hoặc rỗng";
inline constexpr std::string_view kSyntaxMapMissingComma = "Giá trị ánh xạ trực tiếp: thiếu dấu ',' giữa các cặp khóa/giá trị";
inline constexpr std::string_view kSyntaxInvalidListLiteral = "Giá trị danh sách trực tiếp không hợp lệ";
inline constexpr std::string_view kSyntaxUnsupportedListValue = "Giá trị danh sách trực tiếp hiện chỉ hỗ trợ phần tử vô hướng";
inline constexpr std::string_view kSyntaxListMissingComma = "Giá trị danh sách trực tiếp thiếu dấu phẩy";
inline constexpr std::string_view kSyntaxListMissingValue = "Giá trị danh sách trực tiếp thiếu phần tử";
inline constexpr std::string_view kSyntaxUnexpectedCommaOutsideCall = "convertToPostfix: dấu ',' xuất hiện ngoài lời gọi hàm";
inline constexpr std::string_view kSyntaxMismatchedExpressionParens = "convertToPostfix: cặp ngoặc tròn không khớp";
inline constexpr std::string_view kSyntaxUnknownExpressionToken = "convertToPostfix: từ tố không xác định '{0}'";
inline constexpr std::string_view kSyntaxUnsupportedOperator = "compileExpr: toán tử chưa được hỗ trợ: {0}";
inline constexpr std::string_view kSyntaxUnbalancedBrackets = "bộ phân tích cú pháp: cặp ngoặc vuông không cân bằng";

// Semantic diagnostics.
inline constexpr std::string_view kSemanticPrivateMethodAccess = "Không thể gọi phương thức riêng tư '{0}' từ phạm vi hiện tại";
inline constexpr std::string_view kSemanticProtectedMethodAccess = "Không thể gọi phương thức bảo vệ '{0}' từ phạm vi hiện tại";
inline constexpr std::string_view kSemanticUnsupportedDefaultParameter = "{0}: tham số mặc định chỉ hỗ trợ giá trị trực tiếp (số nguyên, số thực, chuỗi, đúng, sai hoặc rỗng)";
inline constexpr std::string_view kSemanticDuplicateDeclaration = "Khai báo '{0}' bị trùng trong cùng một đơn vị biên dịch";
inline constexpr std::string_view kSemanticMissingDeclarationName = "Không thể xác định tên của khai báo {0}";
inline constexpr std::string_view kSemanticUnresolvedName = "Không thể phân giải tên '{0}' trong phạm vi hiện tại";
inline constexpr std::string_view kSemanticUnresolvedCall = "Không thể phân giải hàm được gọi '{0}'";

// Module/package import diagnostics.
inline constexpr std::string_view kImportMissingTarget = "nhập: thiếu đường dẫn hoặc tên mô-đun";
inline constexpr std::string_view kImportMissingNamespaceAlias = "nhập: thiếu tên không gian tên sau 'như'";
inline constexpr std::string_view kImportCannotOpenFile = "nhập: không thể mở tệp '{0}'";
inline constexpr std::string_view kInternalImportHandlerMissing = "trình biên dịch nội bộ chưa đăng ký bộ xử lý câu lệnh nhập";

// Compiler invariants. These indicate a compiler bug or malformed internal IR.
inline constexpr std::string_view kInternalBlockHandlerDidNotAdvance = "compileBlock: bộ xử lý cho '{0}' không dịch chuyển vị trí (vị trí={1})\nNgữ cảnh: {2}";
inline constexpr std::string_view kInternalStatementDidNotAdvance = "compileBlock: compileStatement không dịch chuyển vị trí tại từ tố '{0}' (vị trí={1})\nNgữ cảnh: {2}";
inline constexpr std::string_view kInternalMissingClosingBlock = "compileBlock: thiếu dấu '}' tại vị trí {0}. Số từ tố={1}. Từ tố tại vị trí này: '{2}'.\nNgữ cảnh: {3}";
inline constexpr std::string_view kInternalNumberParseMismatch = "compileExpr: isNumber=true nhưng stoi không thể chuyển từ tố '{0}'";
inline constexpr std::string_view kInternalMalformedCallToken = "compileExpr: từ tố CALL sai định dạng";
inline constexpr std::string_view kInternalEmptyCallArgCount = "compileExpr: số lượng đối số trống trong từ tố CALL: {0}";
inline constexpr std::string_view kInternalStringPoolIndex = "Bảng chuỗi: chỉ số không hợp lệ";
inline constexpr std::string_view kInternalDirectIrUnsupportedBinaryOperator = "toán tử nhị phân của IR trực tiếp chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrUnsupportedCompoundAssignment = "phép gán kết hợp của IR trực tiếp chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrUnsupportedMapValue = "giá trị ánh xạ của IR trực tiếp chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrUnsupportedListValue = "giá trị danh sách của IR trực tiếp chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrUnsupportedDefaultValue = "giá trị tham số mặc định của IR trực tiếp chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrInvalidValueId = "mã giá trị IR trực tiếp không hợp lệ";
inline constexpr std::string_view kInternalDirectIrInvalidStoreTarget = "đích lưu của IR trực tiếp không hợp lệ";
inline constexpr std::string_view kInternalDirectIrInvalidIndexedStoreTarget = "đích lưu theo chỉ số của IR trực tiếp không hợp lệ";
inline constexpr std::string_view kInternalDirectIrMissingVmFunctionMapping = "lời gọi IR trực tiếp không có ánh xạ tới hàm VM";
inline constexpr std::string_view kInternalDirectIrMissingFunctionNameMapping = "lời gọi IR trực tiếp không có ánh xạ tới tên hàm";
inline constexpr std::string_view kInternalDirectIrMissingLambdaDefaultValue = "tham số lambda của IR trực tiếp không có giá trị mặc định";
inline constexpr std::string_view kInternalDirectIrUnsupportedValue = "bộ phát IR trực tiếp nhận giá trị chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrLoopMissingInitializerTarget = "vòng lặp IR trực tiếp không có đích khởi tạo";
inline constexpr std::string_view kInternalDirectIrSwitchMissingCaseLabel = "khối chọn của IR trực tiếp không có nhãn ca";
inline constexpr std::string_view kInternalDirectIrSwitchEmptyNormalizedLabel = "khối chọn của IR trực tiếp có nhãn chuẩn hóa rỗng";
inline constexpr std::string_view kInternalDirectIrUnsupportedSwitchLabel = "nhãn khối chọn của IR trực tiếp chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrUnsupportedStatement = "bộ phát IR trực tiếp nhận câu lệnh chưa được hỗ trợ";
inline constexpr std::string_view kInternalDirectIrFunctionNotPredeclared = "hàm IR trực tiếp chưa được khai báo trước";
inline constexpr std::string_view kInternalDirectIrMissingParameterDefaultValue = "tham số của IR trực tiếp không có giá trị mặc định";
inline constexpr std::string_view kInternalDirectIrProgramHasUnsupportedRegion = "chương trình chứa vùng IR chưa được bộ phát bytecode trực tiếp hỗ trợ";

} // namespace vietvm::messages
