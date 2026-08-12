#include "vpp/compiler/semantic.h"

#include <algorithm>
#include <array>
#include <utility>

#include "vpp/core/message_constants.h"

namespace vietvm::compiler {
namespace {

using vietvm::frontend::AstExpression;
using vietvm::frontend::AstExpressionKind;
using vietvm::frontend::AstProgram;
using vietvm::frontend::AstStatement;
using vietvm::frontend::AstStatementKind;
using vietvm::frontend::ExprId;
using vietvm::frontend::SourceSpan;
using vietvm::frontend::Token;
using vietvm::frontend::TokenKind;
using vietvm::frontend::kInvalidExprId;

constexpr std::size_t kSymbolSpaceCount = 3;

std::size_t spaceIndex(SymbolSpace space) noexcept {
    return static_cast<std::size_t>(space);
}

SymbolSpace symbolSpace(SemanticSymbolKind kind) noexcept {
    switch (kind) {
        case SemanticSymbolKind::Class: return SymbolSpace::Type;
        case SemanticSymbolKind::ImportAlias: return SymbolSpace::Module;
        default: return SymbolSpace::Value;
    }
}

SemanticVisibility semanticVisibility(vietvm::frontend::AstVisibility visibility) noexcept {
    using vietvm::frontend::AstVisibility;
    switch (visibility) {
        case AstVisibility::Public: return SemanticVisibility::Public;
        case AstVisibility::Private: return SemanticVisibility::Private;
        case AstVisibility::Protected: return SemanticVisibility::Protected;
        case AstVisibility::Unspecified: return SemanticVisibility::Unspecified;
    }
    return SemanticVisibility::Unspecified;
}

bool isCallableKind(SemanticSymbolKind kind) noexcept {
    return kind == SemanticSymbolKind::Function || kind == SemanticSymbolKind::Method;
}

bool isIndirectCallableKind(SemanticSymbolKind kind) noexcept {
    return kind == SemanticSymbolKind::Parameter ||
           kind == SemanticSymbolKind::GlobalVariable ||
           kind == SemanticSymbolKind::LocalVariable ||
           kind == SemanticSymbolKind::CatchVariable;
}

bool isCapturableKind(SemanticSymbolKind kind) noexcept {
    return kind == SemanticSymbolKind::Parameter ||
           kind == SemanticSymbolKind::LocalVariable ||
           kind == SemanticSymbolKind::CatchVariable;
}

bool isNameToken(const Token &token) noexcept {
    return token.kind == TokenKind::Identifier;
}

std::string joinName(const std::vector<Token> &tokens,
                     std::size_t begin,
                     std::size_t end) {
    std::string name;
    for (std::size_t index = begin; index < end; ++index) {
        if (!isNameToken(tokens[index])) break;
        if (!name.empty()) name.push_back(' ');
        name += tokens[index].lexeme;
    }
    return name;
}

// Declaration payloads not yet represented in the AST remain confined to this
// compatibility adapter. Function parameters come from AstStatement directly;
// only import aliases and catch variables still need token ranges.
namespace legacy_payload {

std::pair<std::string, SourceSpan> importAlias(const AstProgram &program,
                                                const AstStatement &statement) {
    const auto &tokens = program.tokens;
    const std::size_t end = std::min(statement.tokenEnd, tokens.size());
    for (std::size_t index = statement.tokenBegin; index + 1 < end; ++index) {
        if (tokens[index].lexeme == "như" && isNameToken(tokens[index + 1])) {
            return {tokens[index + 1].lexeme, tokens[index + 1].span};
        }
    }
    return {};
}

std::pair<std::string, SourceSpan> catchVariable(const AstProgram &program,
                                                  const AstStatement &statement,
                                                  std::size_t catchBlockIndex) {
    if (catchBlockIndex >= statement.children.size()) return {};
    const auto &tokens = program.tokens;
    const std::size_t begin = catchBlockIndex == 0
        ? statement.tokenBegin
        : statement.children[catchBlockIndex - 1].tokenEnd;
    const std::size_t end = std::min(statement.children[catchBlockIndex].tokenBegin,
                                     tokens.size());

    for (std::size_t index = begin; index + 2 < end; ++index) {
        if (tokens[index].lexeme == "bắt lỗi" && tokens[index + 1].lexeme == "(" &&
            isNameToken(tokens[index + 2])) {
            return {tokens[index + 2].lexeme, tokens[index + 2].span};
        }
    }
    return {};
}

} // namespace legacy_payload

struct ScopeBindings {
    std::array<std::unordered_map<std::string, SymbolId>, kSymbolSpaceCount> names;
};

struct LookupResult {
    SymbolId symbol = kInvalidSymbolId;
    ScopeId scope = kInvalidScopeId;
    std::size_t depth = 0;
};

class Analyzer {
public:
    Analyzer(const AstProgram &program,
             const SemanticEnvironment &environment,
             ResolutionPolicy policy)
        : program_(program), environment_(environment), policy_(policy) {
        model_.expressionBindings.resize(program_.expressions.size());
        model_.callBindings.resize(program_.expressions.size());
        model_.expressionScopes.assign(program_.expressions.size(), kInvalidScopeId);
        for (ExprId id = 0; id < program_.expressions.size(); ++id) {
            model_.expressionBindings[id].expression = id;
            model_.callBindings[id].expression = id;
        }
    }

    SemanticModel run() {
        model_.globalScope = addScope(ScopeKind::Global, kInvalidScopeId, program_.span);
        addExternalSymbols();
        buildStatementList(program_.statements, model_.globalScope, kInvalidSymbolId);
        declareExpressionVariables(program_.statements);
        resolveStatementList(program_.statements);
        return std::move(model_);
    }

private:
    ScopeId addScope(ScopeKind kind,
                     ScopeId parent,
                     SourceSpan span,
                     SymbolId ownerSymbol = kInvalidSymbolId,
                     ExprId ownerExpression = kInvalidExprId) {
        const ScopeId id = static_cast<ScopeId>(model_.scopes.size());
        SemanticScope scope;
        scope.id = id;
        scope.kind = kind;
        scope.parent = parent;
        scope.span = span;
        scope.ownerSymbol = ownerSymbol;
        scope.ownerExpression = ownerExpression;
        model_.scopes.push_back(std::move(scope));
        bindings_.emplace_back();
        if (parent != kInvalidScopeId) model_.scopes[parent].children.push_back(id);
        return id;
    }

    void addExternalSymbols() {
        for (const SemanticExternalSymbol &external : environment_.importedSymbols) {
            (void)addSymbol(model_.globalScope, external.kind, external.name,
                            external.name, external.declaration,
                            SemanticVisibility::Public, kInvalidSymbolId,
                            SymbolOrigin::Imported, false, nullptr);
        }
    }

    SymbolId addSymbol(ScopeId scope,
                       SemanticSymbolKind kind,
                       const std::string &lookupName,
                       const std::string &qualifiedName,
                       SourceSpan declaration,
                       SemanticVisibility visibility,
                       SymbolId ownerClass,
                       SymbolOrigin origin,
                       bool diagnoseDuplicate,
                       const AstStatement *declarationStatement,
                       bool *inserted = nullptr) {
        if (inserted != nullptr) *inserted = false;
        if (lookupName.empty()) return kInvalidSymbolId;
        const SymbolSpace space = symbolSpace(kind);
        auto &table = bindings_[scope].names[spaceIndex(space)];
        const auto existing = table.find(lookupName);
        if (existing != table.end()) {
            if (diagnoseDuplicate) {
                model_.diagnostics.push_back({
                    SemanticDiagnosticSeverity::Error,
                    std::string(vietvm::messages::kSemanticDuplicateDeclaration.code),
                    vietvm::messages::messageText(
                        vietvm::messages::kSemanticDuplicateDeclaration, {lookupName}),
                    declaration,
                });
            }
            return existing->second;
        }

        const SymbolId id = static_cast<SymbolId>(model_.symbols.size());
        SemanticSymbol symbol;
        symbol.id = id;
        symbol.kind = kind;
        symbol.name = qualifiedName.empty() ? lookupName : qualifiedName;
        symbol.declaration = declaration;
        symbol.lookupName = lookupName;
        symbol.qualifiedName = qualifiedName.empty() ? lookupName : qualifiedName;
        symbol.space = space;
        symbol.origin = origin;
        symbol.visibility = visibility;
        symbol.declaringScope = scope;
        symbol.ownerClass = ownerClass;
        model_.symbols.push_back(std::move(symbol));
        model_.scopes[scope].declarations.push_back(id);
        table.emplace(lookupName, id);
        const bool qualifiedMethod = kind == SemanticSymbolKind::Method &&
            model_.symbols[id].qualifiedName != model_.symbols[id].lookupName;
        const bool qualifiedImport = origin == SymbolOrigin::Imported &&
            model_.symbols[id].qualifiedName != model_.symbols[id].lookupName;
        if (space == SymbolSpace::Value && (qualifiedMethod || qualifiedImport)) {
            qualifiedValues_.emplace(model_.symbols[id].qualifiedName, id);
        }
        if (declarationStatement != nullptr) {
            model_.declarationSymbols.emplace(declarationStatement->tokenBegin,
                                               static_cast<int>(id));
        }
        if (inserted != nullptr) *inserted = true;
        return id;
    }

    SymbolId predeclare(const AstStatement &statement,
                        ScopeId scope,
                        SymbolId ownerClass) {
        const bool isClass = statement.kind == AstStatementKind::Class;
        const bool isFunction = statement.kind == AstStatementKind::Function;
        if (!isClass && !isFunction) return kInvalidSymbolId;

        if (statement.declarationName.empty()) {
            model_.diagnostics.push_back({
                SemanticDiagnosticSeverity::Error,
                std::string(vietvm::messages::kSemanticMissingDeclarationName.code),
                vietvm::messages::messageText(
                    vietvm::messages::kSemanticMissingDeclarationName,
                    {isFunction ? "hàm" : "lớp"}),
                statement.span,
            });
            return kInvalidSymbolId;
        }

        const SymbolId directOwnerClass = model_.scopes[scope].kind == ScopeKind::Class
            ? ownerClass
            : kInvalidSymbolId;
        const SemanticSymbolKind kind = isClass
            ? SemanticSymbolKind::Class
            : (directOwnerClass == kInvalidSymbolId ? SemanticSymbolKind::Function
                                                     : SemanticSymbolKind::Method);
        const std::string qualifiedName = directOwnerClass == kInvalidSymbolId
            ? statement.declarationName
            : model_.symbols[directOwnerClass].qualifiedName + "." + statement.declarationName;
        SemanticVisibility visibility = semanticVisibility(statement.visibility);
        if (kind == SemanticSymbolKind::Method &&
            visibility == SemanticVisibility::Unspecified) {
            visibility = model_.symbols[directOwnerClass].visibility;
        }
        bool inserted = false;
        const SymbolId symbol = addSymbol(
            scope, kind, statement.declarationName, qualifiedName, statement.span,
            visibility, directOwnerClass, SymbolOrigin::Source, true, &statement, &inserted);
        if (symbol == kInvalidSymbolId || !inserted) return kInvalidSymbolId;

        const ScopeKind scopeKind = isClass ? ScopeKind::Class : ScopeKind::Function;
        const ScopeId memberScope = addScope(scopeKind, scope, statement.span, symbol);
        model_.symbols[symbol].memberScope = memberScope;
        declarationScopes_[statement.tokenBegin] = memberScope;
        return symbol;
    }

    void predeclareStatementList(const std::vector<AstStatement> &statements,
                                 ScopeId scope,
                                 SymbolId ownerClass) {
        for (const AstStatement &statement : statements) {
            (void)predeclare(statement, scope, ownerClass);
            if (statement.kind == AstStatementKind::Import) {
                const auto alias = legacy_payload::importAlias(program_, statement);
                if (!alias.first.empty()) {
                    (void)addSymbol(scope, SemanticSymbolKind::ImportAlias,
                                    alias.first, alias.first, alias.second,
                                    SemanticVisibility::Unspecified, kInvalidSymbolId,
                                    SymbolOrigin::Source, true, &statement);
                }
            }
        }
    }

    void buildStatementList(const std::vector<AstStatement> &statements,
                            ScopeId scope,
                            SymbolId ownerClass) {
        predeclareStatementList(statements, scope, ownerClass);
        for (const AstStatement &statement : statements) {
            buildStatement(statement, scope, ownerClass);
        }
    }

    void addFunctionParameters(const AstStatement &statement, ScopeId functionScope) {
        for (const auto &parameter : statement.parameters) {
            (void)addSymbol(functionScope, SemanticSymbolKind::Parameter,
                            parameter.name, parameter.name, parameter.span,
                            SemanticVisibility::Unspecified, kInvalidSymbolId,
                            SymbolOrigin::Source, true, nullptr);
        }
    }

    void buildOrdinaryBlock(const AstStatement &block,
                            ScopeId parent,
                            SymbolId ownerClass) {
        const ScopeId blockScope = addScope(ScopeKind::Block, parent, block.span);
        model_.statementScopes[block.tokenBegin] = blockScope;
        buildStatementList(block.children, blockScope, ownerClass);
    }

    void buildStatement(const AstStatement &statement,
                        ScopeId containingScope,
                        SymbolId ownerClass) {
        if (statement.kind == AstStatementKind::Class) {
            const auto found = declarationScopes_.find(statement.tokenBegin);
            if (found == declarationScopes_.end()) {
                model_.statementScopes[statement.tokenBegin] = containingScope;
                return;
            }
            const ScopeId classScope = found->second;
            model_.statementScopes[statement.tokenBegin] = classScope;
            const SymbolId classSymbol = model_.scopes[classScope].ownerSymbol;
            for (const AstStatement &body : statement.children) {
                // Class braces delimit the class namespace; they do not add a
                // redundant lexical block scope.
                model_.statementScopes[body.tokenBegin] = classScope;
                buildStatementList(body.children, classScope, classSymbol);
            }
            return;
        }

        if (statement.kind == AstStatementKind::Function) {
            const auto found = declarationScopes_.find(statement.tokenBegin);
            if (found == declarationScopes_.end()) {
                model_.statementScopes[statement.tokenBegin] = containingScope;
                return;
            }
            const ScopeId functionScope = found->second;
            model_.statementScopes[statement.tokenBegin] = functionScope;
            addFunctionParameters(statement, functionScope);
            for (const AstStatement &body : statement.children) {
                buildOrdinaryBlock(body, functionScope, ownerClass);
            }
            return;
        }

        if (statement.kind == AstStatementKind::Block) {
            buildOrdinaryBlock(statement, containingScope, ownerClass);
            return;
        }

        model_.statementScopes[statement.tokenBegin] = containingScope;
        if (statement.kind == AstStatementKind::Try && statement.children.size() > 1) {
            buildOrdinaryBlock(statement.children.front(), containingScope, ownerClass);
            for (std::size_t index = 1; index < statement.children.size(); ++index) {
                const AstStatement &body = statement.children[index];
                const ScopeId catchScope = addScope(ScopeKind::Catch, containingScope, body.span);
                const auto variable = legacy_payload::catchVariable(program_, statement, index);
                if (!variable.first.empty()) {
                    (void)addSymbol(catchScope, SemanticSymbolKind::CatchVariable,
                                    variable.first, variable.first, variable.second,
                                    SemanticVisibility::Unspecified, kInvalidSymbolId,
                                    SymbolOrigin::Source, true, nullptr);
                }
                const ScopeId bodyScope = addScope(ScopeKind::Block, catchScope, body.span);
                model_.statementScopes[body.tokenBegin] = bodyScope;
                buildStatementList(body.children, bodyScope, ownerClass);
            }
            return;
        }

        for (const AstStatement &child : statement.children) {
            buildOrdinaryBlock(child, containingScope, ownerClass);
        }
    }

    ScopeId statementScope(const AstStatement &statement) const noexcept {
        const auto found = model_.statementScopes.find(statement.tokenBegin);
        return found == model_.statementScopes.end() ? model_.globalScope : found->second;
    }

    ScopeId implicitVariableScope(ScopeId scope) const noexcept {
        ScopeId current = scope;
        while (current != kInvalidScopeId) {
            const ScopeKind kind = model_.scopes[current].kind;
            if (kind == ScopeKind::Function || kind == ScopeKind::Lambda) return current;
            current = model_.scopes[current].parent;
        }
        return model_.globalScope;
    }

    SymbolId symbolInScope(ScopeId scope,
                           SymbolSpace space,
                           const std::string &name) const noexcept {
        const auto &table = bindings_[scope].names[spaceIndex(space)];
        const auto found = table.find(name);
        return found == table.end() ? kInvalidSymbolId : found->second;
    }

    LookupResult lookupLexical(const std::string &name, ScopeId scope) const noexcept {
        ScopeId current = scope;
        std::size_t depth = 0;
        while (current != kInvalidScopeId) {
            const SymbolId symbol = symbolInScope(current, SymbolSpace::Value, name);
            if (symbol != kInvalidSymbolId) return {symbol, current, depth};
            current = model_.scopes[current].parent;
            ++depth;
        }
        return {kInvalidSymbolId, kInvalidScopeId, depth};
    }

    void declareAssignmentTarget(const AstExpression &expression, ScopeId scope) {
        ExprId target = kInvalidExprId;
        if (expression.kind == AstExpressionKind::Assignment ||
            expression.kind == AstExpressionKind::CompoundAssignment) {
            target = expression.left;
        } else if (expression.kind == AstExpressionKind::Postfix) {
            target = expression.operand;
        }
        const AstExpression *left = program_.expression(target);
        if (left == nullptr || left->kind != AstExpressionKind::Name || left->text.empty()) return;

        if (lookupLexical(left->text, scope).symbol != kInvalidSymbolId) {
            return;
        }
        const ScopeId declarationScope = implicitVariableScope(scope);
        const SemanticSymbolKind kind = declarationScope == model_.globalScope
            ? SemanticSymbolKind::GlobalVariable
            : SemanticSymbolKind::LocalVariable;
        (void)addSymbol(declarationScope, kind, left->text, left->text, left->span,
                        SemanticVisibility::Unspecified, kInvalidSymbolId,
                        SymbolOrigin::Source, false, nullptr);
    }

    ScopeId ensureLambdaScope(const AstExpression &expression, ScopeId parent) {
        const auto existing = lambdaScopes_.find(expression.id);
        if (existing != lambdaScopes_.end()) return existing->second;
        const ScopeId scope = addScope(ScopeKind::Lambda, parent, expression.span,
                                       kInvalidSymbolId, expression.id);
        lambdaScopes_.emplace(expression.id, scope);
        for (const std::string &parameter : expression.parameters) {
            (void)addSymbol(scope, SemanticSymbolKind::Parameter,
                            parameter, parameter, expression.span,
                            SemanticVisibility::Unspecified, kInvalidSymbolId,
                            SymbolOrigin::Source, true, nullptr);
        }
        return scope;
    }

    void declareExpression(ExprId id, ScopeId scope) {
        const AstExpression *expression = program_.expression(id);
        if (expression == nullptr) return;
        if (model_.expressionScopes[id] == kInvalidScopeId) {
            model_.expressionScopes[id] = scope;
        }

        declareAssignmentTarget(*expression, scope);
        switch (expression->kind) {
            case AstExpressionKind::Unary:
            case AstExpressionKind::Postfix:
                declareExpression(expression->operand, scope);
                break;
            case AstExpressionKind::Binary:
            case AstExpressionKind::Assignment:
            case AstExpressionKind::CompoundAssignment:
                declareExpression(expression->left, scope);
                declareExpression(expression->right, scope);
                break;
            case AstExpressionKind::Call:
                declareExpression(expression->callee, scope);
                for (ExprId argument : expression->arguments) declareExpression(argument, scope);
                break;
            case AstExpressionKind::Lambda:
                (void)ensureLambdaScope(*expression, scope);
                break;
            case AstExpressionKind::MapLiteral:
                for (const auto &entry : expression->mapEntries) {
                    // Bare map keys are encoded names, not variable reads.
                    if (entry.key < model_.expressionScopes.size()) {
                        model_.expressionScopes[entry.key] = scope;
                    }
                    declareExpression(entry.value, scope);
                }
                break;
            case AstExpressionKind::Literal:
            case AstExpressionKind::Name:
                break;
        }
    }

    void declareExpressionVariables(const std::vector<AstStatement> &statements) {
        for (const AstStatement &statement : statements) {
            const ScopeId scope = statementScope(statement);
            if (statement.kind == AstStatementKind::Function) {
                for (const auto &parameter : statement.parameters) {
                    declareExpression(parameter.defaultValue, scope);
                }
            }
            for (ExprId root : statement.expressionRoots) declareExpression(root, scope);
            declareExpressionVariables(statement.children);
        }
    }

    LookupResult lookup(const std::string &name, ScopeId scope) const noexcept {
        const LookupResult lexical = lookupLexical(name, scope);
        if (lexical.symbol != kInvalidSymbolId) return lexical;
        const auto qualified = qualifiedValues_.find(name);
        if (qualified != qualifiedValues_.end()) {
            const SymbolId symbol = qualified->second;
            return {symbol, model_.symbols[symbol].declaringScope, lexical.depth};
        }
        return {};
    }

    bool crossesCallableBoundary(ScopeId from, ScopeId declarationScope) const noexcept {
        ScopeId current = from;
        while (current != kInvalidScopeId && current != declarationScope) {
            if (model_.scopes[current].kind == ScopeKind::Lambda ||
                model_.scopes[current].kind == ScopeKind::Function) {
                return true;
            }
            current = model_.scopes[current].parent;
        }
        return false;
    }

    SymbolId enclosingClass(ScopeId scope) const noexcept {
        ScopeId current = scope;
        while (current != kInvalidScopeId) {
            if (model_.scopes[current].kind == ScopeKind::Class) {
                return model_.scopes[current].ownerSymbol;
            }
            current = model_.scopes[current].parent;
        }
        return kInvalidSymbolId;
    }

    void validateMemberAccess(const SemanticSymbol &symbol,
                              ScopeId useScope,
                              SourceSpan useSpan) {
        if (symbol.kind != SemanticSymbolKind::Method ||
            symbol.visibility == SemanticVisibility::Public ||
            symbol.visibility == SemanticVisibility::Unspecified ||
            enclosingClass(useScope) == symbol.ownerClass) {
            return;
        }

        const auto &definition = symbol.visibility == SemanticVisibility::Private
            ? vietvm::messages::kSemanticPrivateMethodAccess
            : vietvm::messages::kSemanticProtectedMethodAccess;
        model_.diagnostics.push_back({
            SemanticDiagnosticSeverity::Error,
            std::string(definition.code),
            vietvm::messages::messageText(definition, {symbol.qualifiedName}),
            useSpan,
        });
    }

    bool isKnownNative(const std::string &name) const {
        return std::find(environment_.nativeCallables.begin(),
                         environment_.nativeCallables.end(), name) !=
               environment_.nativeCallables.end();
    }

    BindingResult bindName(const AstExpression &expression, ScopeId scope, bool callableUse) {
        BindingResult binding;
        binding.expression = expression.id;
        binding.lookupScope = scope;
        binding.runtimeName = expression.text;
        const LookupResult found = lookup(expression.text, scope);
        if (found.symbol != kInvalidSymbolId) {
            binding.kind = BindingKind::Symbol;
            binding.symbol = found.symbol;
            binding.lookupScope = found.scope;
            binding.lexicalDepth = found.depth;
            binding.captured = isCapturableKind(model_.symbols[found.symbol].kind) &&
                               crossesCallableBoundary(
                                   scope, model_.symbols[found.symbol].declaringScope);
            validateMemberAccess(model_.symbols[found.symbol], scope, expression.span);
        } else if (callableUse && isKnownNative(expression.text)) {
            binding.kind = BindingKind::NativeCallable;
        } else if (callableUse && policy_ == ResolutionPolicy::PreserveLegacy) {
            binding.kind = BindingKind::DynamicName;
        } else if (!callableUse && policy_ == ResolutionPolicy::PreserveLegacy) {
            binding.kind = BindingKind::LegacyImplicitValue;
        } else {
            const auto &definition = callableUse
                ? vietvm::messages::kSemanticUnresolvedCall
                : vietvm::messages::kSemanticUnresolvedName;
            model_.diagnostics.push_back({
                SemanticDiagnosticSeverity::Error,
                std::string(definition.code),
                vietvm::messages::messageText(definition, {expression.text}),
                expression.span,
            });
        }
        model_.expressionBindings[expression.id] = binding;
        return binding;
    }

    void recordCall(const AstExpression &call,
                    const AstExpression *callee,
                    const BindingResult &calleeBinding) {
        CallBinding result;
        result.expression = call.id;
        result.callee = call.callee;
        result.symbol = calleeBinding.symbol;
        result.runtimeName = callee == nullptr ? std::string() : callee->text;
        if (calleeBinding.kind == BindingKind::NativeCallable) {
            result.kind = CallTargetKind::Native;
        } else if (calleeBinding.kind == BindingKind::DynamicName) {
            result.kind = CallTargetKind::DynamicName;
        } else if (calleeBinding.kind == BindingKind::Symbol) {
            const SemanticSymbolKind kind = model_.symbols[calleeBinding.symbol].kind;
            result.kind = isCallableKind(kind)
                ? CallTargetKind::DirectFunction
                : (isIndirectCallableKind(kind) ? CallTargetKind::IndirectValue
                                                : CallTargetKind::Invalid);
        }
        model_.callBindings[call.id] = result;

        if (callee != nullptr && callee->kind == AstExpressionKind::Name) {
            SemanticReference reference;
            reference.name = callee->text;
            reference.span = callee->span;
            reference.dynamic = result.kind == CallTargetKind::DynamicName ||
                                result.kind == CallTargetKind::Native ||
                                result.kind == CallTargetKind::Invalid;
            if (result.symbol != kInvalidSymbolId) {
                reference.resolvedSymbolId = static_cast<int>(result.symbol);
                reference.dynamic = false;
            }
            model_.references.push_back(std::move(reference));
        }
    }

    void resolveExpression(ExprId id, ScopeId scope, bool callableUse = false) {
        const AstExpression *expression = program_.expression(id);
        if (expression == nullptr) return;
        if (model_.expressionScopes[id] == kInvalidScopeId) model_.expressionScopes[id] = scope;

        switch (expression->kind) {
            case AstExpressionKind::Name:
                (void)bindName(*expression, scope, callableUse);
                break;
            case AstExpressionKind::Literal:
                break;
            case AstExpressionKind::Unary:
            case AstExpressionKind::Postfix:
                resolveExpression(expression->operand, scope);
                break;
            case AstExpressionKind::Binary:
            case AstExpressionKind::Assignment:
            case AstExpressionKind::CompoundAssignment:
                resolveExpression(expression->left, scope);
                resolveExpression(expression->right, scope);
                break;
            case AstExpressionKind::Call: {
                resolveExpression(expression->callee, scope, true);
                for (ExprId argument : expression->arguments) resolveExpression(argument, scope);
                const AstExpression *callee = program_.expression(expression->callee);
                const BindingResult calleeBinding = expression->callee < model_.expressionBindings.size()
                    ? model_.expressionBindings[expression->callee]
                    : BindingResult{};
                recordCall(*expression, callee, calleeBinding);
                break;
            }
            case AstExpressionKind::Lambda:
                // The current expression AST records the lambda body token
                // range but does not yet expose its statement roots. Its scope
                // and parameters are still explicit and ready for that link.
                (void)ensureLambdaScope(*expression, scope);
                break;
            case AstExpressionKind::MapLiteral:
                for (const auto &entry : expression->mapEntries) {
                    resolveExpression(entry.value, scope);
                }
                break;
        }
    }

    void resolveFallbackCalls(const AstStatement &statement, ScopeId scope) {
        const bool mayHavePartialRoots = statement.kind == AstStatementKind::Loop;
        if ((!statement.expressionRoots.empty() && !mayHavePartialRoots) ||
            statement.kind == AstStatementKind::Function ||
            statement.kind == AstStatementKind::Class ||
            statement.kind == AstStatementKind::Import ||
            statement.kind == AstStatementKind::Block) {
            return;
        }

        const auto &tokens = program_.tokens;
        std::size_t end = std::min(statement.tokenEnd, tokens.size());
        for (std::size_t index = statement.tokenBegin; index < end; ++index) {
            if (tokens[index].lexeme == "{") {
                end = index;
                break;
            }
        }
        for (std::size_t index = statement.tokenBegin; index + 1 < end; ++index) {
            if (!isNameToken(tokens[index]) || tokens[index + 1].lexeme != "(") continue;
            const bool coveredByExpressionRoot = std::any_of(
                statement.expressionRoots.begin(), statement.expressionRoots.end(),
                [&](ExprId root) {
                    const AstExpression *expression = program_.expression(root);
                    return expression != nullptr &&
                           expression->tokenBegin <= index && index < expression->tokenEnd;
                });
            if (coveredByExpressionRoot) continue;
            std::size_t begin = index;
            while (begin > statement.tokenBegin && isNameToken(tokens[begin - 1])) --begin;
            const std::string name = joinName(tokens, begin, index + 1);
            if (name.empty()) continue;

            const LookupResult resolved = lookup(name, scope);
            SemanticReference reference;
            reference.name = name;
            reference.span = {tokens[begin].span.begin, tokens[index].span.end};
            reference.dynamic = resolved.symbol == kInvalidSymbolId;
            if (!reference.dynamic) reference.resolvedSymbolId = static_cast<int>(resolved.symbol);
            model_.references.push_back(std::move(reference));
        }
    }

    void resolveStatementList(const std::vector<AstStatement> &statements) {
        for (const AstStatement &statement : statements) {
            const ScopeId scope = statementScope(statement);
            if (statement.kind == AstStatementKind::Function) {
                for (const auto &parameter : statement.parameters) {
                    resolveExpression(parameter.defaultValue, scope);
                }
            }
            for (ExprId root : statement.expressionRoots) resolveExpression(root, scope);
            resolveFallbackCalls(statement, scope);
            resolveStatementList(statement.children);
        }
    }

    const AstProgram &program_;
    const SemanticEnvironment &environment_;
    ResolutionPolicy policy_;
    SemanticModel model_;
    std::vector<ScopeBindings> bindings_;
    std::unordered_map<std::size_t, ScopeId> declarationScopes_;
    std::unordered_map<ExprId, ScopeId> lambdaScopes_;
    std::unordered_map<std::string, SymbolId> qualifiedValues_;
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

ScopeId SemanticModel::scopeForStatement(std::size_t tokenBegin) const noexcept {
    const auto found = statementScopes.find(tokenBegin);
    return found == statementScopes.end() ? kInvalidScopeId : found->second;
}

ScopeId SemanticModel::scopeForExpression(ExprId expression) const noexcept {
    return expression < expressionScopes.size() ? expressionScopes[expression]
                                                : kInvalidScopeId;
}

const BindingResult *SemanticModel::bindingForExpression(ExprId expression) const noexcept {
    return expression < expressionBindings.size() ? &expressionBindings[expression] : nullptr;
}

const CallBinding *SemanticModel::callBindingForExpression(ExprId expression) const noexcept {
    if (expression >= callBindings.size() ||
        callBindings[expression].kind == CallTargetKind::Invalid) {
        return nullptr;
    }
    return &callBindings[expression];
}

SemanticModel analyzeSemantics(const AstProgram &program) {
    return analyzeSemantics(program, SemanticEnvironment{}, ResolutionPolicy::PreserveLegacy);
}

SemanticModel analyzeSemantics(const AstProgram &program,
                               const SemanticEnvironment &environment,
                               ResolutionPolicy policy) {
    return Analyzer(program, environment, policy).run();
}

} // namespace vietvm::compiler
