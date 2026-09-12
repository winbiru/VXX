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
    Function,
    Block,
    Lambda,
    Catch,
};

enum class SymbolKind {
    Function,
    Method,
    Class,
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
    NativeCallable,
    DynamicName,
    LegacyImplicitValue,
};

enum class CallTargetKind {
    Invalid,
    DirectFunction,
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
};

struct BindingResult {
    vietvm::frontend::ExprId expression = vietvm::frontend::kInvalidExprId;
    BindingKind kind = BindingKind::Unresolved;
    SymbolId symbol = kInvalidSymbolId;
    ScopeId lookupScope = kInvalidScopeId;
    std::size_t lexicalDepth = 0;
    bool captured = false;
    std::string runtimeName;
};

struct CallBinding {
    vietvm::frontend::ExprId expression = vietvm::frontend::kInvalidExprId;
    vietvm::frontend::ExprId callee = vietvm::frontend::kInvalidExprId;
    CallTargetKind kind = CallTargetKind::Invalid;
    SymbolId symbol = kInvalidSymbolId;
    std::string runtimeName;
};

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

struct SemanticReference {
    std::string name;
    vietvm::frontend::SourceSpan span{};
    int resolvedSymbolId = -1;
    bool dynamic = true;
};

struct SemanticDiagnostic {
    SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::Error;
    std::string message;
    vietvm::frontend::SourceSpan span{};
};

// Imported exports are supplied by a module-indexing layer. Keeping them in an
// input object prevents semantic analysis from reading compiler globals or the
// runtime native-function table.
struct SemanticExternalSymbol {
    std::string name;
    SemanticSymbolKind kind = SemanticSymbolKind::Function;
    vietvm::frontend::SourceSpan declaration{};
};

struct SemanticEnvironment {
    std::vector<SemanticExternalSymbol> importedSymbols;
    std::vector<std::string> nativeCallables;
};

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

    bool hasErrors() const noexcept;
    int symbolForDeclaration(std::size_t tokenBegin) const noexcept;
    ScopeId scopeForStatement(std::size_t tokenBegin) const noexcept;
    ScopeId scopeForExpression(vietvm::frontend::ExprId expression) const noexcept;
    const BindingResult *bindingForExpression(
        vietvm::frontend::ExprId expression) const noexcept;
    const CallBinding *callBindingForExpression(
        vietvm::frontend::ExprId expression) const noexcept;
    const SemanticLambda *lambdaForExpression(
        vietvm::frontend::ExprId expression) const noexcept;
};

// Compatibility entry point. It preserves implicit variables and dynamic-name
// calls while producing bindings for represented expressions, including
// recursive lambda bodies. Tolerant-parser unsupported regions retain token payloads
// for diagnostics/lossless tooling.
SemanticModel analyzeSemantics(const vietvm::frontend::AstProgram &program);

SemanticModel analyzeSemantics(const vietvm::frontend::AstProgram &program,
                               const SemanticEnvironment &environment,
                               ResolutionPolicy policy = ResolutionPolicy::PreserveLegacy);

const char *callTargetKindName(CallTargetKind kind) noexcept;

} // namespace vietvm::compiler
