#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "vpp/frontend/ast.h"

namespace vietvm::compiler {

// Semantic IDs are deterministic indices into one SemanticModel. They are not
// VM function IDs, StringPool indices, or local slots.
using ScopeId = std::uint32_t;
using SymbolId = std::uint32_t;
inline constexpr ScopeId kInvalidScopeId = std::numeric_limits<ScopeId>::max();
inline constexpr SymbolId kInvalidSymbolId = std::numeric_limits<SymbolId>::max();

enum class ScopeKind {
    Global,
    Class,
    Interface,
    Function,
    Block,
    Lambda,
    Catch,
};

enum class SymbolKind {
    Function,
    Method,
    Class,
    Interface,
    Parameter,
    GlobalVariable,
    LocalVariable,
    ImportAlias,
    CatchVariable,

    // Source compatibility for callers of the first semantic API.
    Import = ImportAlias,
};

using SemanticSymbolKind = SymbolKind;

enum class SymbolSpace {
    Value,
    Type,
    Module,
};

enum class SymbolOrigin {
    Source,
    Imported,
};

enum class SemanticVisibility {
    Unspecified,
    Public,
    Private,
    Protected,
};

enum class BindingKind {
    Unresolved,
    Symbol,
    InstanceMember,
    NativeCallable,
    DynamicName,
    LegacyImplicitValue,
};

enum class CallTargetKind {
    Invalid,
    DirectFunction,
    ImportedFunction,
    ClassConstructor,
    InstanceMethod,
    IndirectValue,
    Native,
    DynamicName,
};

enum class ResolutionPolicy {
    PreserveLegacy,
    Strict,
};

enum class SemanticDiagnosticSeverity {
    Warning,
    Error,
};

// Biểu diễn một lexical scope trong semantic model, giữ parent/children, owner và danh sách symbol khai báo để lookup đi theo cây phạm vi.
struct SemanticScope {
    ScopeId id = kInvalidScopeId;
    ScopeKind kind = ScopeKind::Global;
    ScopeId parent = kInvalidScopeId;
    vietvm::frontend::SourceSpan span{};
    SymbolId ownerSymbol = kInvalidSymbolId;
    vietvm::frontend::ExprId ownerExpression = vietvm::frontend::kInvalidExprId;
    std::vector<ScopeId> children;
    std::vector<SymbolId> declarations;
};

// Mô tả một khai báo đã được semantic đăng ký, gồm kind, tên lookup/qualified, visibility, scope sở hữu và quan hệ lớp/giao diện nếu có.
struct SemanticSymbol {
    SymbolId id = kInvalidSymbolId;
    SemanticSymbolKind kind = SemanticSymbolKind::Function;

    // `name` keeps the original API behavior: class methods use their fully
    // qualified name. `lookupName` is the spelling used in the declaring scope.
    std::string name;
    vietvm::frontend::SourceSpan declaration{};
    std::string lookupName;
    std::string qualifiedName;
    SymbolSpace space = SymbolSpace::Value;
    SymbolOrigin origin = SymbolOrigin::Source;
    SemanticVisibility visibility = SemanticVisibility::Unspecified;
    ScopeId declaringScope = kInvalidScopeId;
    ScopeId memberScope = kInvalidScopeId;
    SymbolId ownerClass = kInvalidSymbolId;
    SymbolId superclass = kInvalidSymbolId;
    std::vector<SymbolId> implementedInterfaces;
    std::vector<SymbolId> extendedInterfaces;
    std::size_t parameterCount = 0;
    std::size_t minimumArgumentCount = 0;
    // Lớp instance được suy luận cho biến/receiver khi nguồn gán là `Class(...)`
    // hoặc một symbol đã mang cùng lớp. Dữ liệu này chỉ phục vụ semantic member
    // access; runtime vẫn giữ mô hình động và không phụ thuộc vào type tĩnh.
    SymbolId inferredClass = kInvalidSymbolId;
};

// Ghi cách một expression name/member được phân giải, gồm symbol, lookup depth, receiver/member và cờ capture để lowering không phải lookup lại.
struct BindingResult {
    vietvm::frontend::ExprId expression = vietvm::frontend::kInvalidExprId;
    BindingKind kind = BindingKind::Unresolved;
    SymbolId symbol = kInvalidSymbolId;
    ScopeId lookupScope = kInvalidScopeId;
    std::size_t lexicalDepth = 0;
    bool captured = false;
    std::string runtimeName;
    std::string receiverName;
    std::string memberName;
};

// Ghi metadata dispatch của một call expression, như loại đích, symbol và runtime name, để codegen chọn direct/method/native/dynamic call.
struct CallBinding {
    vietvm::frontend::ExprId expression = vietvm::frontend::kInvalidExprId;
    vietvm::frontend::ExprId callee = vietvm::frontend::kInvalidExprId;
    CallTargetKind kind = CallTargetKind::Invalid;
    SymbolId symbol = kInvalidSymbolId;
    std::string runtimeName;
};

// Lưu scope, body và danh sách capture của một lambda sau semantic analysis để lowering tạo closure metadata đúng.
struct SemanticLambda {
    vietvm::frontend::ExprId expression = vietvm::frontend::kInvalidExprId;
    vietvm::frontend::LambdaId syntax = vietvm::frontend::kInvalidLambdaId;
    ScopeId scope = kInvalidScopeId;
    ScopeId bodyScope = kInvalidScopeId;
    std::vector<SymbolId> parameterSymbols;

    // Unique source symbols captured across this lambda boundary. Individual
    // name uses continue to carry BindingResult::captured and lexicalDepth.
    std::vector<SymbolId> captures;
};

// Ghi một tham chiếu tên trong source cùng symbol đích hoặc trạng thái dynamic; tooling/test dùng record để kiểm tra resolution.
struct SemanticReference {
    std::string name;
    vietvm::frontend::SourceSpan span{};
    int resolvedSymbolId = -1;
    bool dynamic = true;
};

// Biểu diễn một lỗi/cảnh báo semantic với severity, thông điệp và source span để CLI/LSP hiển thị đúng vị trí.
struct SemanticDiagnostic {
    SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::Error;
    std::string message;
    vietvm::frontend::SourceSpan span{};
};

// Mô tả symbol đến từ module/import bên ngoài; analyzer đưa record này vào global scope trước khi phân tích source hiện tại.
struct SemanticExternalSymbol {
    std::string name;
    SemanticSymbolKind kind = SemanticSymbolKind::Function;
    vietvm::frontend::SourceSpan declaration{};
};

// Mô tả một tên có tồn tại trong module đã nhập nhưng không thuộc bề mặt export;
// semantic dùng record này để báo lỗi rõ ràng thay vì coi tên đó là tên động bất kỳ.
struct SemanticHiddenImportedSymbol {
    std::string name;
    std::string moduleIdentity;
    vietvm::frontend::SourceSpan declaration{};
};

// Chứa các external symbol khả dụng cho một lượt semantic analysis, thường được module graph xây từ export của dependency.
struct SemanticEnvironment {
    std::vector<SemanticExternalSymbol> importedSymbols;
    std::vector<SemanticHiddenImportedSymbol> hiddenImportedSymbols;
    std::vector<std::string> nativeCallables;
};

// Sở hữu toàn bộ kết quả semantic: scope, symbol, binding, call binding, lambda, reference và diagnostic của một `AstProgram`.
struct SemanticModel {
    ScopeId globalScope = kInvalidScopeId;
    std::vector<SemanticScope> scopes;
    std::vector<SemanticSymbol> symbols;
    std::vector<BindingResult> expressionBindings;
    std::vector<CallBinding> callBindings;
    std::vector<SemanticLambda> lambdas;
    std::vector<SemanticReference> references;
    std::vector<SemanticDiagnostic> diagnostics;

    // Declaration lookup used by IR lowering. Codegen retains SymbolId and
    // performs a separate symbol-to-bytecode function/slot mapping.
    std::unordered_map<std::size_t, int> declarationSymbols;

    // AST ownership maps make scope selection deterministic for later lowering
    // and tooling without storing pointers into recursive statement vectors.
    std::unordered_map<std::size_t, ScopeId> statementScopes;
    std::vector<ScopeId> expressionScopes;

    // Kiểm tra điều kiện của `hasErrors`.
    bool hasErrors() const noexcept;
    // Tra `SemanticSymbol` tương ứng với một statement khai báo; hàm dùng bảng `declarationSymbols` đã được analyzer tạo thay vì tìm lại theo tên.
    int symbolForDeclaration(std::size_t tokenBegin) const noexcept;
    // Tra semantic scope gắn với một statement; hàm dùng token-begin/key ổn định để trả scope mà analyzer đã xây.
    ScopeId scopeForStatement(std::size_t tokenBegin) const noexcept;
    // Tra semantic scope nơi một expression được phân tích; hàm đọc bảng `expressionScopes` theo ExprId.
    ScopeId scopeForExpression(vietvm::frontend::ExprId expression) const noexcept;
    // Tra kết quả binding của một expression; hàm trả symbol/member/native metadata mà semantic analyzer đã ghi cho ExprId đó.
    const BindingResult *bindingForExpression(
        vietvm::frontend::ExprId expression) const noexcept;
    // Tra metadata đích gọi của một call expression; hàm trả loại dispatch và symbol/runtime name đã được semantic phân giải.
    const CallBinding *callBindingForExpression(
        vietvm::frontend::ExprId expression) const noexcept;
    // Tra `SemanticLambda` tương ứng với expression lambda; hàm ánh xạ ExprId sang record capture/scope đã dựng.
    const SemanticLambda *lambdaForExpression(
        vietvm::frontend::ExprId expression) const noexcept;
};

// Xây dựng `SemanticModel` từ AST; analyzer tạo scope/symbol, phân giải tên và ghi lại binding cùng diagnostic.
SemanticModel analyzeSemantics(const vietvm::frontend::AstProgram &program);

// Xây dựng `SemanticModel` từ AST; analyzer tạo scope/symbol, phân giải tên và ghi lại binding cùng diagnostic.
SemanticModel analyzeSemantics(const vietvm::frontend::AstProgram &program,
                               const SemanticEnvironment &environment,
                               ResolutionPolicy policy = ResolutionPolicy::PreserveLegacy);

// Trả tên ổn định của loại đích gọi semantic để tooling và test nhận biết cách một call sẽ được dispatch.
const char *callTargetKindName(CallTargetKind kind) noexcept;

} // namespace vietvm::compiler
