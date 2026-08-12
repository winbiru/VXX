#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "vpp/frontend/ast.h"

namespace vietvm::compiler {

enum class SemanticSymbolKind {
    Function,
    Class,
    Import,
};

enum class SemanticDiagnosticSeverity {
    Warning,
    Error,
};

struct SemanticSymbol {
    int id = -1;
    SemanticSymbolKind kind = SemanticSymbolKind::Function;
    std::string name;
    vietvm::frontend::SourceSpan declaration{};
};

struct SemanticReference {
    std::string name;
    vietvm::frontend::SourceSpan span{};
    int resolvedSymbolId = -1;
    bool dynamic = true;
};

struct SemanticDiagnostic {
    SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::Error;
    std::string code;
    std::string message;
    vietvm::frontend::SourceSpan span{};
};

struct SemanticModel {
    std::vector<SemanticSymbol> symbols;
    std::vector<SemanticReference> references;
    std::vector<SemanticDiagnostic> diagnostics;

    // Maps an AST statement's first token to its declaration symbol.  It makes
    // lowering deterministic without exposing compiler-global function IDs.
    std::unordered_map<std::size_t, int> declarationSymbols;

    bool hasErrors() const noexcept;
    int symbolForDeclaration(std::size_t tokenBegin) const noexcept;
};

// Resolve declarations and direct callable references.  Unknown names are
// represented as dynamic references rather than errors because V++ currently
// has dynamic name/value behavior and native call fallbacks.
SemanticModel analyzeSemantics(const vietvm::frontend::AstProgram &program);

} // namespace vietvm::compiler
