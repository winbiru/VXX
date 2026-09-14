#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/frontend/token.h"

namespace vietvm::frontend {

using ExprId = std::size_t;
inline constexpr ExprId kInvalidExprId = static_cast<ExprId>(-1);
using LambdaId = std::size_t;
inline constexpr LambdaId kInvalidLambdaId = static_cast<LambdaId>(-1);

enum class AstExpressionKind {
    Literal,
    Name,
    Unary,
    Binary,
    Assignment,
    CompoundAssignment,
    Postfix,
    Call,
    Lambda,
    MapLiteral,
    ListLiteral,
    Index,
};

enum class AstLiteralKind {
    None,
    Integer,
    Float,
    String,
    Boolean,
    Null,
};

enum class AstVisibility {
    Unspecified,
    Public,
    Private,
    Protected,
};

// Conditional shape is explicit syntax metadata, not inferred later from the
// number of parsed blocks. `Unstructured` keeps tolerant/legacy forms such as
// single-statement branches and `else if` on the compatibility path.
enum class AstConditionalForm {
    Unstructured,
    IfBlock,
    IfElseBlocks,
};

enum class AstLoopForm {
    Unstructured,
    ForBlock,
};

enum class AstClassForm {
    Unstructured,
    MethodBlock,
};

enum class AstInterfaceForm {
    Unstructured,
    MethodSignatures,
};

enum class AstImportForm {
    Unstructured,
    LocalSourceFile,
};

enum class AstSwitchForm {
    Unstructured,
    Structured,
};

enum class AstTryForm {
    Unstructured,
    TryCatchBlocks,
};

enum class AstSwitchArmKind {
    Case,
    Default,
};

// Biểu diễn một nhánh `ca`/`mặc định` trong AST, giữ label ExprId và chỉ số block con để semantic/lowering duyệt đúng nhánh.
struct AstSwitchArm {
    AstSwitchArmKind kind = AstSwitchArmKind::Case;
    SourceSpan span{};
    SourceSpan labelSpan{};
    ExprId label = kInvalidExprId;
    std::size_t bodyChildIndex = 0;
    bool hasColon = false;
    bool prefixedByCase = false;
};

// Lưu tên, source span và ExprId giá trị mặc định của một tham số hàm/lambda sau parser.
struct AstParameter {
    std::string name;
    SourceSpan span{};
    bool hasDefault = false;
    ExprId defaultValue = kInvalidExprId;
};

// Lưu một tham chiếu kiểu theo tên cùng vị trí nguồn; dùng cho danh sách lớp/giao diện cha và các giao diện mà lớp cam kết triển khai.
struct AstTypeReference {
    std::string name;
    SourceSpan span{};
};

// Lưu cấu trúc câu lệnh nhập đã parse như target, alias, quoted và semicolon để module resolver không phải phân tích token thô.
struct AstImportSpec {
    std::string target;
    SourceSpan targetSpan{};
    bool quoted = false;
    std::string alias;
    SourceSpan aliasSpan{};
    bool hasSemicolon = false;
    // `công khai nhập ...` re-exports the imported module surface.
    bool reExport = false;
};

// Biểu diễn một cặp khóa–giá trị trong map literal bằng ExprId, cho phép arena biểu thức giữ cấu trúc lồng nhau ổn định.
struct AstMapEntry {
    ExprId key = kInvalidExprId;
    ExprId value = kInvalidExprId;
    SourceSpan span{};
};

// Biểu diễn một expression trong arena AST, giữ kind/literal/span/token range cùng operand/child ids để semantic duyệt không cần reparsing.
struct AstExpression {
    ExprId id = kInvalidExprId;
    AstExpressionKind kind = AstExpressionKind::Name;
    AstLiteralKind literalKind = AstLiteralKind::None;
    SourceSpan span{};
    std::size_t tokenBegin = 0;
    std::size_t tokenEnd = 0;

    // Literal spelling, name, or operator depending on kind.
    std::string text;

    ExprId operand = kInvalidExprId;
    ExprId left = kInvalidExprId;
    ExprId right = kInvalidExprId;
    ExprId callee = kInvalidExprId;

    // Preserve the source-level `gọi name(...)` form independently of the
    // node's token range. Grouping may widen tokenBegin/tokenEnd, but it must
    // not erase this grammar distinction before semantic lowering/codegen.
    bool explicitCall = false;
    std::vector<ExprId> arguments;

    // Lambda declarations live in AstProgram::lambdas. Keeping the recursive
    // statement body outside this expression node makes arena IDs stable and
    // avoids a recursive AstExpression/AstStatement value type.
    LambdaId lambdaId = kInvalidLambdaId;

    std::vector<AstMapEntry> mapEntries;
    std::vector<ExprId> listElements;
};

// This is intentionally an untyped, lossless syntax AST.  The language is
// dynamically typed today, so type information belongs to a future policy and
// must not be fabricated in the parser.
enum class AstStatementKind {
    Empty,
    Block,
    Import,
    Function,
    Class,
    Interface,
    Conditional,
    Loop,
    Switch,
    Return,
    Print,
    Break,
    Continue,
    Throw,
    Try,
    Expression,
    Unknown,
};

// Biểu diễn một câu lệnh AST có cấu trúc, gồm loại, span, token range, khai báo, expression roots và các statement con.
struct AstStatement {
    AstStatementKind kind = AstStatementKind::Unknown;
    SourceSpan span{};

    // Ranges refer to AstProgram::tokens and keep the source representation
    // lossless while the legacy bytecode backend is being migrated.
    std::size_t tokenBegin = 0;
    std::size_t tokenEnd = 0;

    // Filled for declarations where the parser can identify a stable name.
    std::string declarationName;
    AstVisibility visibility = AstVisibility::Unspecified;
    AstImportForm importForm = AstImportForm::Unstructured;
    AstImportSpec importSpec;
    AstClassForm classForm = AstClassForm::Unstructured;
    std::string superclassName;
    SourceSpan superclassSpan{};
    std::vector<AstTypeReference> implementedInterfaces;
    AstInterfaceForm interfaceForm = AstInterfaceForm::Unstructured;
    std::vector<AstTypeReference> extendedInterfaces;
    AstConditionalForm conditionalForm = AstConditionalForm::Unstructured;
    AstLoopForm loopForm = AstLoopForm::Unstructured;
    AstSwitchForm switchForm = AstSwitchForm::Unstructured;
    AstTryForm tryForm = AstTryForm::Unstructured;
    std::string catchVariable;
    SourceSpan catchVariableSpan{};
    std::vector<AstParameter> parameters;
    std::vector<AstSwitchArm> switchArms;

    // Expression roots owned by AstProgram::expressions. Most simple
    // statements have one root; loop headers may have several in source order.
    // An empty list means the tolerant parser retained only the token range.
    std::vector<ExprId> expressionRoots;

    // Direct brace-delimited statement children.
    std::vector<AstStatement> children;
};

// Lưu một lambda trong arena riêng với tham số, body và expression owner để semantic tạo scope/capture độc lập.
struct AstLambda {
    LambdaId id = kInvalidLambdaId;
    ExprId expression = kInvalidExprId;
    SourceSpan span{};
    std::vector<AstParameter> parameters;
    AstStatement body;
};

// Là nút gốc của frontend, sở hữu token, statement, expression arena và lambda arena của toàn bộ source file.
struct AstProgram {
    SourceSpan span{};
    std::vector<Token> tokens;
    std::vector<AstExpression> expressions;
    std::vector<AstLambda> lambdas;
    std::vector<AstStatement> statements;

    // Trả `expression` hiện tại từ trạng thái nội bộ; accessor chỉ đọc dữ liệu để caller/test kiểm tra mà không làm thay đổi đối tượng.
    const AstExpression *expression(ExprId id) const noexcept {
        return id < expressions.size() ? &expressions[id] : nullptr;
    }

    // Trả `lambda` hiện tại từ trạng thái nội bộ; accessor chỉ đọc dữ liệu để caller/test kiểm tra mà không làm thay đổi đối tượng.
    const AstLambda *lambda(LambdaId id) const noexcept {
        return id < lambdas.size() ? &lambdas[id] : nullptr;
    }
};

// Trả tên văn bản ổn định cho AST câu lệnh loại; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astStatementKindName(AstStatementKind kind) noexcept;
// Trả tên văn bản ổn định cho AST biểu thức loại; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astExpressionKindName(AstExpressionKind kind) noexcept;
// Trả tên văn bản ổn định cho AST giá trị trực tiếp loại; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astLiteralKindName(AstLiteralKind kind) noexcept;
// Trả tên văn bản ổn định cho AST phạm vi truy cập; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astVisibilityName(AstVisibility visibility) noexcept;
// Trả tên văn bản ổn định cho AST nhập form; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astImportFormName(AstImportForm form) noexcept;
// Trả tên văn bản ổn định cho AST lớp form; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astClassFormName(AstClassForm form) noexcept;
// Trả tên văn bản ổn định cho dạng khai báo giao diện để tooling/test hiển thị metadata parser nhất quán.
const char *astInterfaceFormName(AstInterfaceForm form) noexcept;
// Trả tên văn bản ổn định cho AST conditional form; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astConditionalFormName(AstConditionalForm form) noexcept;
// Trả tên văn bản ổn định cho AST vòng lặp form; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astLoopFormName(AstLoopForm form) noexcept;
// Trả tên văn bản ổn định cho AST khối chọn form; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astSwitchFormName(AstSwitchForm form) noexcept;
// Trả tên văn bản ổn định cho AST khối chọn arm loại; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astSwitchArmKindName(AstSwitchArmKind kind) noexcept;
// Trả tên văn bản ổn định cho AST khối thử form; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *astTryFormName(AstTryForm form) noexcept;

} // namespace vietvm::frontend
