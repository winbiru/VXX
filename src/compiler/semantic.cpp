#include "vpp/compiler/semantic.h"

#include <unordered_map>
#include <utility>

#include "vpp/core/message_constants.h"

namespace vietvm::compiler {
namespace {

using vietvm::frontend::AstProgram;
using vietvm::frontend::AstStatement;
using vietvm::frontend::AstStatementKind;
using vietvm::frontend::SourceSpan;
using vietvm::frontend::Token;
using vietvm::frontend::TokenKind;

bool isNameToken(const Token &token) {
    return token.kind == TokenKind::Identifier;
}

std::string joinName(const std::vector<Token> &tokens, std::size_t begin, std::size_t end) {
    std::string name;
    for (std::size_t index = begin; index < end; ++index) {
        if (!isNameToken(tokens[index])) break;
        if (!name.empty()) name.push_back(' ');
        name += tokens[index].lexeme;
    }
    return name;
}

struct Analyzer {
    const AstProgram &program;
    SemanticModel model;
    std::unordered_map<std::string, int> declarationsByKey;
    std::unordered_map<std::string, int> functionsByName;

    void addDeclaration(const AstStatement &statement,
                        SemanticSymbolKind kind,
                        const std::string &name) {
        if (name.empty()) {
            const char *kindText = kind == SemanticSymbolKind::Function ? "hàm" : "lớp";
            model.diagnostics.push_back({
                SemanticDiagnosticSeverity::Error,
                std::string(vietvm::messages::kSemanticMissingDeclarationName.code),
                vietvm::messages::messageText(vietvm::messages::kSemanticMissingDeclarationName,
                                               {kindText}),
                statement.span,
            });
            return;
        }

        const std::string declarationKey =
            std::string(kind == SemanticSymbolKind::Function ? "function:" : "class:") + name;
        const auto existing = declarationsByKey.find(declarationKey);
        if (existing != declarationsByKey.end()) {
            model.diagnostics.push_back({
                SemanticDiagnosticSeverity::Error,
                std::string(vietvm::messages::kSemanticDuplicateDeclaration.code),
                vietvm::messages::messageText(vietvm::messages::kSemanticDuplicateDeclaration,
                                               {name}),
                statement.span,
            });
            return;
        }

        const int id = static_cast<int>(model.symbols.size());
        model.symbols.push_back({id, kind, name, statement.span});
        model.declarationSymbols.emplace(statement.tokenBegin, id);
        declarationsByKey.emplace(declarationKey, id);
        if (kind == SemanticSymbolKind::Function) {
            functionsByName.emplace(name, id);
        }
    }

    void collectDeclarations(const AstStatement &statement, const std::string &classPrefix) {
        std::string childPrefix = classPrefix;
        if (statement.kind == AstStatementKind::Class) {
            addDeclaration(statement, SemanticSymbolKind::Class, statement.declarationName);
            childPrefix = statement.declarationName.empty()
                ? classPrefix
                : (classPrefix.empty() ? statement.declarationName : classPrefix + "." + statement.declarationName);
        } else if (statement.kind == AstStatementKind::Function) {
            const std::string fullName = classPrefix.empty() || statement.declarationName.empty()
                ? statement.declarationName
                : classPrefix + "." + statement.declarationName;
            addDeclaration(statement, SemanticSymbolKind::Function, fullName);
        }

        for (const AstStatement &child : statement.children) {
            collectDeclarations(child, childPrefix);
        }
    }

    void collectReferences() {
        const std::vector<Token> &tokens = program.tokens;
        for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
            if (!isNameToken(tokens[index]) || tokens[index + 1].lexeme != "(") continue;

            std::size_t begin = index;
            while (begin > 0 && isNameToken(tokens[begin - 1])) --begin;

            // A function name in `hàm ten(...)` is a declaration, not a call.
            if (begin > 0 && tokens[begin - 1].lexeme == "hàm") continue;

            const std::string name = joinName(tokens, begin, index + 1);
            if (name.empty()) continue;

            const auto resolved = functionsByName.find(name);
            SemanticReference reference;
            reference.name = name;
            reference.span = {tokens[begin].span.begin, tokens[index].span.end};
            reference.dynamic = resolved == functionsByName.end();
            if (!reference.dynamic) reference.resolvedSymbolId = resolved->second;
            model.references.push_back(std::move(reference));
        }
    }
};

} // namespace

bool SemanticModel::hasErrors() const noexcept {
    for (const SemanticDiagnostic &diagnostic : diagnostics) {
        if (diagnostic.severity == SemanticDiagnosticSeverity::Error) return true;
    }
    return false;
}

int SemanticModel::symbolForDeclaration(std::size_t tokenBegin) const noexcept {
    const auto found = declarationSymbols.find(tokenBegin);
    return found == declarationSymbols.end() ? -1 : found->second;
}

SemanticModel analyzeSemantics(const vietvm::frontend::AstProgram &program) {
    Analyzer analyzer{program, {}, {}, {}};
    for (const AstStatement &statement : program.statements) {
        analyzer.collectDeclarations(statement, "");
    }
    analyzer.collectReferences();
    return std::move(analyzer.model);
}

} // namespace vietvm::compiler
