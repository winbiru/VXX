#pragma once

namespace vietvm::messages {

// Lexer/compiler diagnostics: VPP-LEX, VPP-SYN, VPP-SEM, VPP-IMP and VPP-INT.
// This domain header is included by message_constants.h after MessageDefinition
// and the formatting helpers are declared; keep it dependency-free to avoid an
// umbrella-header include cycle.

// Lexer diagnostics.
inline constexpr MessageDefinition kLexerUnclosedString{
    "VPP-LEX-001", "Chuỗi không được đóng (thiếu dấu nháy kết thúc)"};
inline constexpr MessageDefinition kLexerUnclosedComment{
    "VPP-LEX-002", "Comment block không được đóng (thiếu */)"};
inline constexpr MessageDefinition kLexerInvalidCharacter{
    "VPP-LEX-003", "Ký tự không hợp lệ: '{0}' (mã: {1})"};
inline constexpr MessageDefinition kLexerInvalidEscapeSequence{
    "VPP-LEX-004", "Escape sequence không hợp lệ: \\{0}"};
inline constexpr MessageDefinition kLexerUnexpectedToken{
    "VPP-LEX-005", "Token không mong đợi: '{0}'"};
inline constexpr MessageDefinition kLexerUnexpectedTokenWithExpectation{
    "VPP-LEX-012", "Token không mong đợi: '{0}', mong đợi: {1}"};
inline constexpr MessageDefinition kLexerInvalidNumber{
    "VPP-LEX-006", "Số không hợp lệ: '{0}'"};
inline constexpr MessageDefinition kLexerInvalidIdentifier{
    "VPP-LEX-007", "Tên định danh không hợp lệ: '{0}'"};
inline constexpr MessageDefinition kLexerEmptyIdentifier{
    "VPP-LEX-008", "Tên định danh không được rỗng"};
inline constexpr MessageDefinition kLexerInvalidString{
    "VPP-LEX-009", "Chuỗi không hợp lệ: '{0}'"};
inline constexpr MessageDefinition kLexerMalformedString{
    "VPP-LEX-010", "Chuỗi không được đóng đúng cách: '{0}'"};
inline constexpr MessageDefinition kLexerMissingVietnameseAccent{
    "VPP-LEX-011", "Từ khóa phải có dấu tiếng Việt: '{0}'"};

// Legacy integer-variable helper diagnostics.  The helper lives in lexer.cpp
// for compatibility, but its failures are compiler-facing rather than lexical.
inline constexpr MessageDefinition kCompilerVariableIdOutOfRange{
    "VPP-CMP-001", "getVarValueInt: varId ngoài phạm vi (id={0}, size={1})"};
inline constexpr MessageDefinition kCompilerVariableNotInteger{
    "VPP-CMP-002", "getVarValueInt: giá trị biến không phải số hợp lệ: '{0}'"};
inline constexpr MessageDefinition kCompilerVariableIntegerOverflow{
    "VPP-CMP-003", "getVarValueInt: giá trị số quá lớn: '{0}'"};
inline constexpr MessageDefinition kCompilerUnsupportedVariableValue{
    "VPP-CMP-004", "getVarValueInt: kiểu giá trị không hỗ trợ"};

// Source syntax diagnostics.
inline constexpr MessageDefinition kSyntaxExpectedOpeningParen{
    "VPP-SYN-001", "extractParens: expected '('"};
inline constexpr MessageDefinition kSyntaxUnbalancedParens{
    "VPP-SYN-002", "extractParens: unbalanced parentheses"};
inline constexpr MessageDefinition kSyntaxExpectedOpeningBlock{
    "VPP-SYN-003", "extractBlock: expected '{'"};
inline constexpr MessageDefinition kSyntaxUnbalancedBraces{
    "VPP-SYN-004", "extractBlock: unbalanced braces"};
inline constexpr MessageDefinition kSyntaxMissingAssignmentOperator{
    "VPP-SYN-005", "extractAssignedVar: no '=' found"};
inline constexpr MessageDefinition kSyntaxInvalidLoopParts{
    "VPP-SYN-006", "compileLoop: cannot parse loop parts"};
inline constexpr MessageDefinition kSyntaxElseWithoutIf{
    "VPP-SYN-007", "'hoặc' phải đi ngay sau một khối 'nếu'"};
inline constexpr MessageDefinition kSyntaxMissingFunctionName{
    "VPP-SYN-008", "compile: thiếu tên hàm sau 'hàm'"};
inline constexpr MessageDefinition kSyntaxInvalidFunctionName{
    "VPP-SYN-009", "compile: tên hàm không hợp lệ sau 'hàm'"};
inline constexpr MessageDefinition kSyntaxExpectedBlockAtPosition{
    "VPP-SYN-010", "compileBlock: expected '{' at pos={0}, found token='{1}'"};
inline constexpr MessageDefinition kSyntaxModifierBeforeFunction{
    "VPP-SYN-011",
    "Dùng cú pháp 'hàm <quyền>' (ví dụ: 'hàm {0} tenHam(...)') thay vì '<quyền> hàm'"};
inline constexpr MessageDefinition kSyntaxMissingClassName{
    "VPP-SYN-012", "lớp: thiếu tên lớp"};
inline constexpr MessageDefinition kSyntaxMissingClassOpeningBlock{
    "VPP-SYN-013", "lớp: thiếu '{' sau tên lớp"};
inline constexpr MessageDefinition kSyntaxUnsupportedClassMember{
    "VPP-SYN-014", "lớp: hiện chỉ hỗ trợ khai báo hàm trong thân lớp"};
inline constexpr MessageDefinition kSyntaxMissingClassClosingBlock{
    "VPP-SYN-015", "lớp: thiếu '}' kết thúc lớp"};
inline constexpr MessageDefinition kSyntaxMissingCallName{
    "VPP-SYN-016", "gọi: thiếu tên hàm"};
inline constexpr MessageDefinition kSyntaxInvalidCallName{
    "VPP-SYN-017", "gọi: tên hàm không hợp lệ"};
inline constexpr MessageDefinition kSyntaxMissingCallOpeningParen{
    "VPP-SYN-018", "gọi: thiếu '(' sau tên hàm"};
inline constexpr MessageDefinition kSyntaxReturnMustUseVe{
    "VPP-SYN-019", "'trả' phải đi cùng 'về'"};
inline constexpr MessageDefinition kSyntaxSwitchWrongEntryToken{
    "VPP-SYN-020", "compileSwitch: không phải token 'chọn' tại vị trí pos"};
inline constexpr MessageDefinition kSyntaxSwitchMissingOpeningBlock{
    "VPP-SYN-021", "compileSwitch: thiếu dấu '{'"};
inline constexpr MessageDefinition kSyntaxSwitchMissingCaseExpression{
    "VPP-SYN-022", "compileSwitch: thiếu biểu thức sau 'ca'"};
inline constexpr MessageDefinition kSyntaxSwitchInvalidToken{
    "VPP-SYN-023", "Token không hợp lệ trong khối chọn: '{0}' (normalized='{1}')"};
inline constexpr MessageDefinition kSyntaxSwitchMissingClosingBlock{
    "VPP-SYN-024", "compileSwitch: thiếu dấu '}'"};
inline constexpr MessageDefinition kSyntaxInvalidMapLiteral{
    "VPP-SYN-025", "Map literal không hợp lệ"};
inline constexpr MessageDefinition kSyntaxInvalidMapKey{
    "VPP-SYN-026", "Map literal: key phải là chuỗi hoặc identifier"};
inline constexpr MessageDefinition kSyntaxMapMissingColon{
    "VPP-SYN-027", "Map literal: thiếu dấu ':' sau key"};
inline constexpr MessageDefinition kSyntaxMapMissingValue{
    "VPP-SYN-028", "Map literal: thiếu value"};
inline constexpr MessageDefinition kSyntaxUnsupportedMapValue{
    "VPP-SYN-029", "Map literal: value chỉ hỗ trợ int/float/string/đúng/sai/rỗng"};
inline constexpr MessageDefinition kSyntaxMapMissingComma{
    "VPP-SYN-030", "Map literal: thiếu dấu ',' giữa các cặp key/value"};
inline constexpr MessageDefinition kSyntaxUnexpectedCommaOutsideCall{
    "VPP-SYN-031", "convertToPostfix: unexpected ',' outside function call"};
inline constexpr MessageDefinition kSyntaxMismatchedExpressionParens{
    "VPP-SYN-032", "convertToPostfix: mismatched parens"};
inline constexpr MessageDefinition kSyntaxUnknownExpressionToken{
    "VPP-SYN-033", "convertToPostfix: unknown token '{0}'"};
inline constexpr MessageDefinition kSyntaxUnsupportedOperator{
    "VPP-SYN-034", "compileExpr: unsupported operator {0}"};
inline constexpr MessageDefinition kSyntaxUnbalancedBrackets{
    "VPP-SYN-035", "parser: unbalanced square brackets"};

// Semantic diagnostics.
inline constexpr MessageDefinition kSemanticPrivateMethodAccess{
    "VPP-SEM-001", "Không thể gọi phương thức riêng tư '{0}' từ phạm vi hiện tại"};
inline constexpr MessageDefinition kSemanticProtectedMethodAccess{
    "VPP-SEM-002", "Không thể gọi phương thức bảo vệ '{0}' từ phạm vi hiện tại"};
inline constexpr MessageDefinition kSemanticUnsupportedDefaultParameter{
    "VPP-SEM-003", "{0}: tham số mặc định chỉ hỗ trợ literal (int/float/string/đúng/sai/rỗng)"};
inline constexpr MessageDefinition kSemanticDuplicateDeclaration{
    "VPP-SEM-004", "Khai báo '{0}' bị trùng trong cùng một đơn vị biên dịch"};
inline constexpr MessageDefinition kSemanticMissingDeclarationName{
    "VPP-SEM-005", "Không thể xác định tên của khai báo {0}"};
inline constexpr MessageDefinition kSemanticUnresolvedName{
    "VPP-SEM-006", "Không thể phân giải tên '{0}' trong phạm vi hiện tại"};
inline constexpr MessageDefinition kSemanticUnresolvedCall{
    "VPP-SEM-007", "Không thể phân giải hàm được gọi '{0}'"};

// Module/package import diagnostics.
inline constexpr MessageDefinition kImportMissingTarget{
    "VPP-IMP-001", "nhập: thiếu đường dẫn hoặc tên module"};
inline constexpr MessageDefinition kImportMissingNamespaceAlias{
    "VPP-IMP-002", "nhập: thiếu tên namespace sau 'như'"};
inline constexpr MessageDefinition kImportCannotOpenFile{
    "VPP-IMP-003", "nhập: không thể mở file '{0}'"};

// Compiler invariants.  These indicate a compiler bug or a malformed internal
// intermediate representation, but are still rendered as coded diagnostics.
inline constexpr MessageDefinition kInternalBlockHandlerDidNotAdvance{
    "VPP-INT-001",
    "compileBlock: handler for '{0}' did not advance pos (pos={1})\nContext: {2}"};
inline constexpr MessageDefinition kInternalStatementDidNotAdvance{
    "VPP-INT-002",
    "compileBlock: compileStatement did not advance pos at token '{0}' (pos={1})\nContext: {2}"};
inline constexpr MessageDefinition kInternalMissingClosingBlock{
    "VPP-INT-003",
    "compileBlock: missing '}' at pos={0}. Token count={1}. Token at pos: '{2}'.\nContext: {3}"};
inline constexpr MessageDefinition kInternalNumberParseMismatch{
    "VPP-INT-004", "compileExpr: isNumber=true but stoi failed for token: '{0}'"};
inline constexpr MessageDefinition kInternalMalformedCallToken{
    "VPP-INT-005", "compileExpr: malformed CALL token"};
inline constexpr MessageDefinition kInternalEmptyCallArgCount{
    "VPP-INT-006", "compileExpr: empty argc in CALL token: {0}"};
inline constexpr MessageDefinition kInternalStringPoolIndex{
    "VPP-INT-007", "StringPool: index"};

} // namespace vietvm::messages
