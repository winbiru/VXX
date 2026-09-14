#include "vpp/compiler/semantic.h"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

#include "vpp/core/message_constants.h"

namespace vietvm::compiler {
namespace {

using vietvm::frontend::AstExpression;
using vietvm::frontend::AstExpressionKind;
using vietvm::frontend::AstLambda;
using vietvm::frontend::AstProgram;
using vietvm::frontend::AstStatement;
using vietvm::frontend::AstStatementKind;
using vietvm::frontend::ExprId;
using vietvm::frontend::SourceSpan;
using vietvm::frontend::Token;
using vietvm::frontend::TokenKind;
using vietvm::frontend::kInvalidExprId;

constexpr std::size_t kSymbolSpaceCount = 3;
constexpr std::array<std::string_view, 2> kImplicitReceiverNames = {
    "mình",
    "gốc",
};

// Chuyển `SymbolSpace` thành chỉ số mảng bảng tên; semantic analyzer dùng chỉ số này để tách namespace value/type/module.
std::size_t spaceIndex(SymbolSpace space) noexcept {
    return static_cast<std::size_t>(space);
}

// Xác định một `SemanticSymbolKind` thuộc không gian value, type hay module; kết quả quyết định bảng tên nào nhận symbol.
SymbolSpace symbolSpace(SemanticSymbolKind kind) noexcept {
    switch (kind) {
        case SemanticSymbolKind::Class: return SymbolSpace::Type;
        case SemanticSymbolKind::Interface: return SymbolSpace::Type;
        case SemanticSymbolKind::ImportAlias: return SymbolSpace::Module;
        default: return SymbolSpace::Value;
    }
}

// Chuyển modifier visibility từ AST sang enum semantic tương ứng để bước kiểm tra truy cập không phụ thuộc frontend enum.
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

// Kiểm tra điều kiện của `isCallableKind`.
bool isCallableKind(SemanticSymbolKind kind) noexcept {
    return kind == SemanticSymbolKind::Function || kind == SemanticSymbolKind::Method;
}

// Kiểm tra điều kiện của `isIndirectCallableKind`.
bool isIndirectCallableKind(SemanticSymbolKind kind) noexcept {
    return kind == SemanticSymbolKind::Parameter ||
           kind == SemanticSymbolKind::GlobalVariable ||
           kind == SemanticSymbolKind::LocalVariable ||
           kind == SemanticSymbolKind::CatchVariable;
}

// Tách định danh đầy đủ runtime thành viên; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
std::pair<std::string, std::string> splitQualifiedRuntimeMember(
    const std::string &name) {
    const std::size_t separator = name.find('.');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= name.size()) {
        return {};
    }
    return {name.substr(0, separator), name.substr(separator + 1)};
}

// Kiểm tra điều kiện của `isCapturableKind`.
bool isCapturableKind(SemanticSymbolKind kind) noexcept {
    return kind == SemanticSymbolKind::Parameter ||
           kind == SemanticSymbolKind::LocalVariable ||
           kind == SemanticSymbolKind::CatchVariable;
}

// Kiểm tra điều kiện của `isNameToken`.
bool isNameToken(const Token &token) noexcept {
    return token.kind == TokenKind::Identifier;
}

// Ghép tên; hàm nối các phần tử theo thứ tự bằng dấu phân cách quy định để tạo kết quả duy nhất.
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
// compatibility adapter. Function parameters and structured local-file import
// aliases come from AstStatement directly; unstructured imports and catches
// still need token ranges.
namespace legacy_payload {

// Lấy bí danh hiệu lực của một câu lệnh import; hàm ưu tiên alias explicit và suy ra tên mặc định từ đích khi cần.
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

// Lấy tên biến được bind trong `bắt lỗi`; hàm đọc metadata parser để semantic tạo symbol trong catch scope.
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

// Lưu các bảng tên của một semantic scope theo từng `SymbolSpace`; analyzer tra struct này để phân biệt value, type và module cùng tên.
struct ScopeBindings {
    std::array<std::unordered_map<std::string, SymbolId>, kSymbolSpaceCount> names;
};

// Mang kết quả lexical lookup gồm symbol tìm được và độ sâu/phạm vi liên quan; analyzer dùng record này để tạo binding/capture chính xác.
struct LookupResult {
    SymbolId symbol = kInvalidSymbolId;
    ScopeId scope = kInvalidScopeId;
    std::size_t depth = 0;
};

// Điều phối phân tích ngữ nghĩa cho bộ phân tích; lớp xây scope/symbol, phân giải tham chiếu và tích lũy diagnostic trong một lượt phân tích.
class Analyzer {
public:
    // Khởi tạo `Analyzer` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
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

    // Chạy vòng lặp VM từ bytecode hiện tại; mỗi bước đọc opcode tại program counter và chuyển tới handler tương ứng cho tới khi dừng.
    SemanticModel run() {
        model_.globalScope = addScope(ScopeKind::Global, kInvalidScopeId, program_.span);
        addExternalSymbols();
        buildStatementList(program_.statements, model_.globalScope, kInvalidSymbolId);
        validateClassInheritance();
        validateMethodOverrides();
        validateInterfaceInheritance();
        validateInterfaceContracts();
        declareExpressionVariables(program_.statements);
        resolveStatementList(program_.statements);
        return std::move(model_);
    }

private:
    // Thêm phạm vi; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
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

    // Thêm bên ngoài symbols; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
    void addExternalSymbols() {
        for (const SemanticExternalSymbol &external : environment_.importedSymbols) {
            (void)addSymbol(model_.globalScope, external.kind, external.name,
                            external.name, external.declaration,
                            SemanticVisibility::Public, kInvalidSymbolId,
                            SymbolOrigin::Imported, false, nullptr);
        }
    }

    // Thêm ký hiệu; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
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

    // Đăng ký trước symbol top-level trước khi phân tích thân; nhờ đó semantic hỗ trợ forward reference cho hàm/lớp theo chính sách hiện tại.
    SymbolId predeclare(const AstStatement &statement,
                        ScopeId scope,
                        SymbolId ownerClass) {
        const bool isClass = statement.kind == AstStatementKind::Class;
        const bool isInterface = statement.kind == AstStatementKind::Interface;
        const bool isFunction = statement.kind == AstStatementKind::Function;
        if (!isClass && !isInterface && !isFunction) return kInvalidSymbolId;

        if (statement.declarationName.empty()) {
            model_.diagnostics.push_back({
                SemanticDiagnosticSeverity::Error,
                vietvm::messages::messageText(
                    vietvm::messages::kSemanticMissingDeclarationName,
                    {isFunction ? "hàm" : (isInterface ? "giao diện" : "lớp")}),
                statement.span,
            });
            return kInvalidSymbolId;
        }

        const bool insideType = model_.scopes[scope].kind == ScopeKind::Class ||
                                model_.scopes[scope].kind == ScopeKind::Interface;
        const SymbolId directOwnerClass = insideType
            ? ownerClass
            : kInvalidSymbolId;
        const SemanticSymbolKind kind = isClass
            ? SemanticSymbolKind::Class
            : (isInterface
                   ? SemanticSymbolKind::Interface
                   : (directOwnerClass == kInvalidSymbolId ? SemanticSymbolKind::Function
                                                            : SemanticSymbolKind::Method));
        const std::string qualifiedName = directOwnerClass == kInvalidSymbolId
            ? statement.declarationName
            : model_.symbols[directOwnerClass].qualifiedName + "." + statement.declarationName;
        SemanticVisibility visibility = semanticVisibility(statement.visibility);
        if (kind == SemanticSymbolKind::Method && directOwnerClass != kInvalidSymbolId &&
            model_.symbols[directOwnerClass].kind == SemanticSymbolKind::Interface &&
            visibility == SemanticVisibility::Unspecified) {
            visibility = SemanticVisibility::Public;
        } else if (kind == SemanticSymbolKind::Method &&
            visibility == SemanticVisibility::Unspecified) {
            visibility = model_.symbols[directOwnerClass].visibility;
        }
        bool inserted = false;
        const SymbolId symbol = addSymbol(
            scope, kind, statement.declarationName, qualifiedName, statement.span,
            visibility, directOwnerClass, SymbolOrigin::Source, true, &statement, &inserted);
        if (symbol == kInvalidSymbolId || !inserted) return kInvalidSymbolId;

        if (isFunction) {
            SemanticSymbol &callable = model_.symbols[symbol];
            callable.parameterCount = statement.parameters.size();
            callable.minimumArgumentCount = 0;
            for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
                if (!statement.parameters[index].hasDefault) {
                    callable.minimumArgumentCount = index + 1;
                }
            }
        }

        const ScopeKind scopeKind = isClass
            ? ScopeKind::Class
            : (isInterface ? ScopeKind::Interface : ScopeKind::Function);
        const ScopeId memberScope = addScope(scopeKind, scope, statement.span, symbol);
        model_.symbols[symbol].memberScope = memberScope;
        declarationScopes_[statement.tokenBegin] = memberScope;
        return symbol;
    }

    // Đăng ký trước toàn bộ khai báo trong một danh sách statement; hàm duyệt siblings trước khi bước build scope đi vào thân từng khai báo.
    void predeclareStatementList(const std::vector<AstStatement> &statements,
                                 ScopeId scope,
                                 SymbolId ownerClass) {
        for (const AstStatement &statement : statements) {
            (void)predeclare(statement, scope, ownerClass);
            if (statement.kind == AstStatementKind::Import) {
                const auto alias = statement.importForm ==
                        vietvm::frontend::AstImportForm::LocalSourceFile
                    ? std::make_pair(statement.importSpec.alias,
                                     statement.importSpec.aliasSpan)
                    : legacy_payload::importAlias(program_, statement);
                if (!alias.first.empty()) {
                    (void)addSymbol(scope, SemanticSymbolKind::ImportAlias,
                                    alias.first, alias.first, alias.second,
                                    SemanticVisibility::Unspecified, kInvalidSymbolId,
                                    SymbolOrigin::Source, true, &statement);
                }
            }
        }
    }

    // Dựng câu lệnh danh sách; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
    void buildStatementList(const std::vector<AstStatement> &statements,
                            ScopeId scope,
                            SymbolId ownerClass) {
        predeclareStatementList(statements, scope, ownerClass);
        for (const AstStatement &statement : statements) {
            buildStatement(statement, scope, ownerClass);
        }
    }

    // Thêm hàm parameters; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
    void addFunctionParameters(const AstStatement &statement,
                               ScopeId functionScope,
                               SymbolId ownerClass) {
        if (ownerClass != kInvalidSymbolId && ownerClass < model_.symbols.size() &&
            model_.symbols[ownerClass].kind == SemanticSymbolKind::Class) {
            for (std::string_view receiverName : kImplicitReceiverNames) {
                const SymbolId receiver = addSymbol(
                    functionScope, SemanticSymbolKind::Parameter,
                    std::string(receiverName), std::string(receiverName),
                    statement.span, SemanticVisibility::Private, ownerClass,
                    SymbolOrigin::Source, true, nullptr);
                if (receiver != kInvalidSymbolId) {
                    model_.symbols[receiver].inferredClass = ownerClass;
                }
            }
        }
        for (const auto &parameter : statement.parameters) {
            (void)addSymbol(functionScope, SemanticSymbolKind::Parameter,
                            parameter.name, parameter.name, parameter.span,
                            SemanticVisibility::Unspecified, kInvalidSymbolId,
                            SymbolOrigin::Source, true, nullptr);
        }
    }

    // Dựng ordinary khối; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
    void buildOrdinaryBlock(const AstStatement &block,
                            ScopeId parent,
                            SymbolId ownerClass) {
        const ScopeId blockScope = addScope(ScopeKind::Block, parent, block.span);
        model_.statementScopes[block.tokenBegin] = blockScope;
        buildStatementList(block.children, blockScope, ownerClass);
    }

    // Dựng câu lệnh; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
    void buildStatement(const AstStatement &statement,
                        ScopeId containingScope,
                        SymbolId ownerClass) {
        if (statement.kind == AstStatementKind::Interface) {
            const auto found = declarationScopes_.find(statement.tokenBegin);
            if (found == declarationScopes_.end()) {
                model_.statementScopes[statement.tokenBegin] = containingScope;
                return;
            }
            const ScopeId interfaceScope = found->second;
            model_.statementScopes[statement.tokenBegin] = interfaceScope;
            const SymbolId interfaceSymbol = model_.scopes[interfaceScope].ownerSymbol;

            if (statement.interfaceForm !=
                vietvm::frontend::AstInterfaceForm::MethodSignatures) {
                model_.diagnostics.push_back({
                    SemanticDiagnosticSeverity::Error,
                    vietvm::messages::messageText(
                        vietvm::messages::kSemanticInvalidInterfaceBody,
                        {statement.declarationName}),
                    statement.span,
                });
            }

            std::unordered_set<SymbolId> seenParents;
            for (const auto &reference : statement.extendedInterfaces) {
                const LookupResult parent = lookupTypeLexical(reference.name, containingScope);
                if (parent.symbol == kInvalidSymbolId) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticUnresolvedInterface,
                            {reference.name}),
                        reference.span,
                    });
                    continue;
                }
                if (model_.symbols[parent.symbol].kind != SemanticSymbolKind::Interface) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticExpectedInterface,
                            {reference.name}),
                        reference.span,
                    });
                    continue;
                }
                if (parent.symbol == interfaceSymbol) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticSelfInterfaceInheritance,
                            {statement.declarationName}),
                        reference.span,
                    });
                    continue;
                }
                if (!seenParents.insert(parent.symbol).second) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticDuplicateInterface,
                            {reference.name}),
                        reference.span,
                    });
                    continue;
                }
                model_.symbols[interfaceSymbol].extendedInterfaces.push_back(parent.symbol);
            }

            for (const AstStatement &body : statement.children) {
                model_.statementScopes[body.tokenBegin] = interfaceScope;
                buildStatementList(body.children, interfaceScope, interfaceSymbol);
                for (const AstStatement &member : body.children) {
                    if (member.kind != AstStatementKind::Function) continue;
                    if (member.visibility == vietvm::frontend::AstVisibility::Private ||
                        member.visibility == vietvm::frontend::AstVisibility::Protected) {
                        model_.diagnostics.push_back({
                            SemanticDiagnosticSeverity::Error,
                            vietvm::messages::messageText(
                                vietvm::messages::kSemanticInterfaceMethodMustBePublic,
                                {member.declarationName, statement.declarationName}),
                            member.span,
                        });
                    }
                }
            }
            return;
        }

        if (statement.kind == AstStatementKind::Class) {
            const auto found = declarationScopes_.find(statement.tokenBegin);
            if (found == declarationScopes_.end()) {
                model_.statementScopes[statement.tokenBegin] = containingScope;
                return;
            }
            const ScopeId classScope = found->second;
            model_.statementScopes[statement.tokenBegin] = classScope;
            const SymbolId classSymbol = model_.scopes[classScope].ownerSymbol;
            if (!statement.superclassName.empty()) {
                const LookupResult superclass =
                    lookupTypeLexical(statement.superclassName, containingScope);
                if (superclass.symbol == kInvalidSymbolId ||
                    model_.symbols[superclass.symbol].kind != SemanticSymbolKind::Class) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticUnresolvedSuperclass,
                            {statement.superclassName}),
                        statement.superclassSpan,
                    });
                } else if (superclass.symbol == classSymbol) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticSelfInheritance,
                            {statement.declarationName}),
                        statement.superclassSpan,
                    });
                } else {
                    model_.symbols[classSymbol].superclass = superclass.symbol;
                }
            }
            std::unordered_set<SymbolId> seenInterfaces;
            for (const auto &reference : statement.implementedInterfaces) {
                const LookupResult implemented =
                    lookupTypeLexical(reference.name, containingScope);
                if (implemented.symbol == kInvalidSymbolId) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticUnresolvedInterface,
                            {reference.name}),
                        reference.span,
                    });
                    continue;
                }
                if (model_.symbols[implemented.symbol].kind !=
                    SemanticSymbolKind::Interface) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticExpectedInterface,
                            {reference.name}),
                        reference.span,
                    });
                    continue;
                }
                if (!seenInterfaces.insert(implemented.symbol).second) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticDuplicateInterface,
                            {reference.name}),
                        reference.span,
                    });
                    continue;
                }
                model_.symbols[classSymbol].implementedInterfaces.push_back(
                    implemented.symbol);
            }
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
            addFunctionParameters(statement, functionScope, ownerClass);
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
                const auto variable =
                    statement.tryForm ==
                            vietvm::frontend::AstTryForm::TryCatchBlocks &&
                        index == 1
                    ? std::make_pair(statement.catchVariable,
                                     statement.catchVariableSpan)
                    : legacy_payload::catchVariable(program_, statement, index);
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

    // Kiểm tra điều kiện của `validateClassInheritance`.
    void validateClassInheritance() {
        std::vector<unsigned char> state(model_.symbols.size(), 0);
        const auto visit = [&](const auto &self, SymbolId symbol) -> void {
            if (symbol == kInvalidSymbolId || symbol >= model_.symbols.size()) return;
            if (state[symbol] == 2) return;
            if (state[symbol] == 1) {
                const SemanticSymbol &klass = model_.symbols[symbol];
                model_.diagnostics.push_back({
                    SemanticDiagnosticSeverity::Error,
                    vietvm::messages::messageText(
                        vietvm::messages::kSemanticInheritanceCycle,
                        {klass.lookupName}),
                    klass.declaration,
                });
                return;
            }
            state[symbol] = 1;
            const SymbolId parent = model_.symbols[symbol].superclass;
            if (parent != kInvalidSymbolId) self(self, parent);
            state[symbol] = 2;
        };
        for (const SemanticSymbol &symbol : model_.symbols) {
            if (symbol.kind == SemanticSymbolKind::Class) visit(visit, symbol.id);
        }
    }

    // Trả mức độ mở của visibility để kiểm tra override. `Unspecified` ở semantic
    // hiện có hành vi public khi truy cập, nên được xếp cùng mức với `Public`.
    static int overrideVisibilityRank(SemanticVisibility visibility) noexcept {
        switch (visibility) {
            case SemanticVisibility::Private: return 0;
            case SemanticVisibility::Protected: return 1;
            case SemanticVisibility::Public:
            case SemanticVisibility::Unspecified: return 2;
        }
        return 2;
    }

    // Tìm method cùng tên gần nhất trong chuỗi lớp cha có thể tham gia override.
    // Method private thuộc riêng lớp khai báo nên bị bỏ qua; constructor `khởi tạo`
    // có lifecycle riêng và không tham gia override giữa lớp cha/con.
    SymbolId lookupOverriddenMethod(SymbolId classSymbol,
                                    const std::string &memberName) const noexcept {
        if (memberName == "khởi tạo" || classSymbol == kInvalidSymbolId ||
            classSymbol >= model_.symbols.size()) {
            return kInvalidSymbolId;
        }

        std::unordered_set<SymbolId> visited;
        for (SymbolId current = model_.symbols[classSymbol].superclass;
             current != kInvalidSymbolId && visited.insert(current).second;) {
            if (current >= model_.symbols.size()) return kInvalidSymbolId;
            const ScopeId memberScope = model_.symbols[current].memberScope;
            if (memberScope != kInvalidScopeId && memberScope < bindings_.size()) {
                const SymbolId member = symbolInScope(
                    memberScope, SymbolSpace::Value, memberName);
                if (member != kInvalidSymbolId && member < model_.symbols.size() &&
                    model_.symbols[member].kind == SemanticSymbolKind::Method &&
                    model_.symbols[member].visibility != SemanticVisibility::Private) {
                    return member;
                }
            }
            current = model_.symbols[current].superclass;
        }
        return kInvalidSymbolId;
    }

    // Khóa contract override của 1.0: cùng tên trong lớp con là override nếu tìm
    // thấy method không-private ở ancestor; override phải giữ nguyên arity và không
    // được thu hẹp visibility. Constructor không phải override và được kiểm tra theo
    // contract constructor riêng.
    void validateMethodOverrides() {
        for (const SemanticSymbol &klass : model_.symbols) {
            if (klass.kind != SemanticSymbolKind::Class ||
                klass.memberScope == kInvalidScopeId ||
                klass.memberScope >= model_.scopes.size()) {
                continue;
            }

            for (SymbolId declaration : model_.scopes[klass.memberScope].declarations) {
                if (declaration >= model_.symbols.size()) continue;
                const SemanticSymbol &method = model_.symbols[declaration];
                if (method.kind != SemanticSymbolKind::Method ||
                    method.lookupName == "khởi tạo") {
                    continue;
                }

                const SymbolId overriddenId =
                    lookupOverriddenMethod(klass.id, method.lookupName);
                if (overriddenId == kInvalidSymbolId) continue;
                const SemanticSymbol &overridden = model_.symbols[overriddenId];

                if (method.parameterCount != overridden.parameterCount) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticOverrideArityMismatch,
                            {method.lookupName, klass.lookupName,
                             std::to_string(method.parameterCount),
                             model_.symbols[overridden.ownerClass].lookupName,
                             std::to_string(overridden.parameterCount)}),
                        method.declaration,
                    });
                }

                if (overrideVisibilityRank(method.visibility) <
                    overrideVisibilityRank(overridden.visibility)) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticOverrideVisibilityNarrowing,
                            {method.lookupName, klass.lookupName,
                             model_.symbols[overridden.ownerClass].lookupName}),
                        method.declaration,
                    });
                }
            }
        }
    }

    // Phát hiện chu trình khi một giao diện kế thừa một hoặc nhiều giao diện khác; DFS dùng trạng thái ba màu giống kiểm tra kế thừa lớp.
    void validateInterfaceInheritance() {
        std::vector<unsigned char> state(model_.symbols.size(), 0);
        const auto visit = [&](const auto &self, SymbolId symbol) -> void {
            if (symbol == kInvalidSymbolId || symbol >= model_.symbols.size()) return;
            if (state[symbol] == 2) return;
            if (state[symbol] == 1) {
                const SemanticSymbol &interfaceSymbol = model_.symbols[symbol];
                model_.diagnostics.push_back({
                    SemanticDiagnosticSeverity::Error,
                    vietvm::messages::messageText(
                        vietvm::messages::kSemanticInterfaceInheritanceCycle,
                        {interfaceSymbol.lookupName}),
                    interfaceSymbol.declaration,
                });
                return;
            }
            state[symbol] = 1;
            for (SymbolId parent : model_.symbols[symbol].extendedInterfaces) {
                self(self, parent);
            }
            state[symbol] = 2;
        };
        for (const SemanticSymbol &symbol : model_.symbols) {
            if (symbol.kind == SemanticSymbolKind::Interface) visit(visit, symbol.id);
        }
    }

    // Thu thập các phương thức bắt buộc của một giao diện, bao gồm hợp đồng kế thừa từ mọi giao diện cha và chặn vòng lặp bằng tập visited.
    void collectRequiredInterfaceMethods(SymbolId interfaceSymbol,
                                         std::unordered_set<SymbolId> &visited,
                                         std::vector<SymbolId> &methods) const {
        if (interfaceSymbol == kInvalidSymbolId || interfaceSymbol >= model_.symbols.size() ||
            !visited.insert(interfaceSymbol).second) {
            return;
        }
        const SemanticSymbol &interfaceInfo = model_.symbols[interfaceSymbol];
        for (SymbolId parent : interfaceInfo.extendedInterfaces) {
            collectRequiredInterfaceMethods(parent, visited, methods);
        }
        const ScopeId memberScope = interfaceInfo.memberScope;
        if (memberScope == kInvalidScopeId || memberScope >= model_.scopes.size()) return;
        for (SymbolId declaration : model_.scopes[memberScope].declarations) {
            if (declaration < model_.symbols.size() &&
                model_.symbols[declaration].kind == SemanticSymbolKind::Method) {
                methods.push_back(declaration);
            }
        }
    }

    // Kiểm tra mỗi lớp đã cung cấp đầy đủ phương thức mà các giao diện yêu cầu; phương thức kế thừa từ lớp cha được chấp nhận nếu đúng arity và truy cập công khai.
    void validateInterfaceContracts() {
        for (const SemanticSymbol &klass : model_.symbols) {
            if (klass.kind != SemanticSymbolKind::Class) continue;
            for (SymbolId interfaceSymbol : klass.implementedInterfaces) {
                if (interfaceSymbol >= model_.symbols.size()) continue;
                std::unordered_set<SymbolId> visited;
                std::vector<SymbolId> requiredMethods;
                collectRequiredInterfaceMethods(
                    interfaceSymbol, visited, requiredMethods);
                for (SymbolId requiredId : requiredMethods) {
                    const SemanticSymbol &required = model_.symbols[requiredId];
                    const SymbolId implementation =
                        lookupMethodSymbol(klass.id, required.lookupName);
                    if (implementation == kInvalidSymbolId ||
                        model_.symbols[implementation].parameterCount !=
                            required.parameterCount) {
                        model_.diagnostics.push_back({
                            SemanticDiagnosticSeverity::Error,
                            vietvm::messages::messageText(
                                vietvm::messages::kSemanticMissingInterfaceMethod,
                                {klass.lookupName, required.lookupName,
                                 std::to_string(required.parameterCount),
                                 model_.symbols[interfaceSymbol].lookupName}),
                            klass.declaration,
                        });
                        continue;
                    }
                    const SemanticVisibility visibility =
                        model_.symbols[implementation].visibility;
                    if (visibility == SemanticVisibility::Private ||
                        visibility == SemanticVisibility::Protected) {
                        model_.diagnostics.push_back({
                            SemanticDiagnosticSeverity::Error,
                            vietvm::messages::messageText(
                                vietvm::messages::kSemanticInterfaceImplementationMustBePublic,
                                {klass.lookupName, required.lookupName,
                                 model_.symbols[interfaceSymbol].lookupName}),
                            model_.symbols[implementation].declaration,
                        });
                    }
                }
            }
        }
    }

    // Trả scope semantic gắn với statement; hàm dùng bảng scope đã xây và fallback về scope cha khi statement không tạo scope riêng.
    ScopeId statementScope(const AstStatement &statement) const noexcept {
        const auto found = model_.statementScopes.find(statement.tokenBegin);
        return found == model_.statementScopes.end() ? model_.globalScope : found->second;
    }

    // Chọn scope để khai báo biến implicit phát sinh từ phép gán; hàm đi qua block/lambda theo quy tắc lexical của V++.
    ScopeId implicitVariableScope(ScopeId scope) const noexcept {
        ScopeId current = scope;
        while (current != kInvalidScopeId) {
            const ScopeKind kind = model_.scopes[current].kind;
            if (kind == ScopeKind::Function || kind == ScopeKind::Lambda) return current;
            current = model_.scopes[current].parent;
        }
        return model_.globalScope;
    }

    // Tra một symbol theo tên trong đúng một scope và symbol-space; hàm không đi lên parent nên caller kiểm soát chiến lược lexical lookup.
    SymbolId symbolInScope(ScopeId scope,
                           SymbolSpace space,
                           const std::string &name) const noexcept {
        const auto &table = bindings_[scope].names[spaceIndex(space)];
        const auto found = table.find(name);
        return found == table.end() ? kInvalidSymbolId : found->second;
    }

    // Tra cứu lexical; hàm tìm trong bảng/bản đồ tương ứng và trả kết quả đã phân giải mà không tự tạo phần tử mới.
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

    // Tra cứu kiểu lexical; hàm tìm trong bảng/bản đồ tương ứng và trả kết quả đã phân giải mà không tự tạo phần tử mới.
    LookupResult lookupTypeLexical(const std::string &name, ScopeId scope) const noexcept {
        ScopeId current = scope;
        std::size_t depth = 0;
        while (current != kInvalidScopeId) {
            const SymbolId symbol = symbolInScope(current, SymbolSpace::Type, name);
            if (symbol != kInvalidSymbolId) return {symbol, current, depth};
            current = model_.scopes[current].parent;
            ++depth;
        }
        return {kInvalidSymbolId, kInvalidScopeId, depth};
    }

    // Khai báo hoặc tái sử dụng symbol ở vế trái phép gán; hàm nhận diện target hợp lệ và ghi binding để các lần đọc sau dùng cùng symbol.
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

        const auto member = splitQualifiedRuntimeMember(left->text);
        if (!member.first.empty()) {
            const LookupResult receiver = lookupLexical(member.first, scope);
            if (receiver.symbol != kInvalidSymbolId &&
                isIndirectCallableKind(model_.symbols[receiver.symbol].kind)) {
                return;
            }
        }

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

    // Bảo đảm lambda đã có semantic scope; hàm tạo scope/parameter symbol một lần rồi tái sử dụng khi nhiều pha cần truy cập.
    ScopeId ensureLambdaScope(const AstExpression &expression, ScopeId parent) {
        const auto existing = lambdaScopes_.find(expression.id);
        if (existing != lambdaScopes_.end()) return existing->second;
        const ScopeId scope = addScope(ScopeKind::Lambda, parent, expression.span,
                                       kInvalidSymbolId, expression.id);
        lambdaScopes_.emplace(expression.id, scope);

        SemanticLambda semanticLambda;
        semanticLambda.expression = expression.id;
        semanticLambda.syntax = expression.lambdaId;
        semanticLambda.scope = scope;
        const std::size_t semanticIndex = model_.lambdas.size();
        model_.lambdas.push_back(std::move(semanticLambda));
        lambdaModelIndices_.emplace(expression.id, semanticIndex);
        lambdaScopeModels_.emplace(scope, semanticIndex);

        const AstLambda *lambda = program_.lambda(expression.lambdaId);
        if (lambda == nullptr || lambda->expression != expression.id) return scope;
        for (const auto &parameter : lambda->parameters) {
            const SymbolId symbol = addSymbol(
                scope, SemanticSymbolKind::Parameter, parameter.name,
                parameter.name, parameter.span, SemanticVisibility::Unspecified,
                kInvalidSymbolId, SymbolOrigin::Source, true, nullptr);
            model_.lambdas[semanticIndex].parameterSymbols.push_back(symbol);
        }
        return scope;
    }

    // Bảo đảm thân lambda đã được dựng semantic; hàm build block/symbol của body đúng một lần sau khi lambda scope sẵn sàng.
    ScopeId ensureLambdaBody(const AstExpression &expression, ScopeId parent) {
        const ScopeId lambdaScope = ensureLambdaScope(expression, parent);
        if (!builtLambdaBodies_.insert(expression.id).second) return lambdaScope;

        const AstLambda *lambda = program_.lambda(expression.lambdaId);
        if (lambda == nullptr || lambda->expression != expression.id ||
            lambda->body.kind != AstStatementKind::Block) {
            return lambdaScope;
        }
        buildOrdinaryBlock(lambda->body, lambdaScope, enclosingClass(parent));
        const auto semanticIndex = lambdaModelIndices_.find(expression.id);
        if (semanticIndex != lambdaModelIndices_.end()) {
            model_.lambdas[semanticIndex->second].bodyScope =
                model_.scopeForStatement(lambda->body.tokenBegin);
        }
        return lambdaScope;
    }

    // Duyệt một expression để khai báo các target phát sinh; hàm đi đệ quy assignment/lambda trước pha resolve tên.
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
                {
                    const ScopeId lambdaScope = ensureLambdaBody(*expression, scope);
                    const AstLambda *lambda = program_.lambda(expression->lambdaId);
                    if (lambda != nullptr && lambda->expression == expression->id &&
                        declaredLambdaBodies_.insert(expression->id).second) {
                        for (const auto &parameter : lambda->parameters) {
                            declareExpression(parameter.defaultValue, lambdaScope);
                        }
                        declareExpressionVariables(lambda->body.children);
                    }
                }
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
            case AstExpressionKind::ListLiteral:
                for (ExprId element : expression->listElements) {
                    declareExpression(element, scope);
                }
                break;
            case AstExpressionKind::Index:
                declareExpression(expression->left, scope);
                declareExpression(expression->right, scope);
                break;
            case AstExpressionKind::Literal:
            case AstExpressionKind::Name:
                break;
        }
    }

    // Duyệt danh sách statement và khai báo biến implicit trong các expression root trước khi bước resolve tham chiếu chạy.
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

    // Tra cứu lookup; hàm tìm trong bảng/bản đồ tương ứng và trả kết quả đã phân giải mà không tự tạo phần tử mới.
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

    // Kiểm tra lookup từ scope dùng tới scope khai báo có đi qua biên hàm/lambda hay không; kết quả quyết định biến có phải capture.
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

    // Ghi các symbol bên ngoài mà lambda sử dụng; hàm loại trùng và lưu capture vào `SemanticLambda` để lowering/runtime dùng.
    void recordLambdaCaptures(ScopeId useScope, const SemanticSymbol &symbol) {
        ScopeId current = useScope;
        while (current != kInvalidScopeId && current != symbol.declaringScope) {
            if (model_.scopes[current].kind == ScopeKind::Lambda) {
                const auto owner = lambdaScopeModels_.find(current);
                if (owner != lambdaScopeModels_.end()) {
                    auto &captures = model_.lambdas[owner->second].captures;
                    if (std::find(captures.begin(), captures.end(), symbol.id) ==
                        captures.end()) {
                        captures.push_back(symbol.id);
                    }
                }
            }
            current = model_.scopes[current].parent;
        }
    }

    // Tìm lớp bao quanh một scope semantic; hàm đi dần parent scope cho tới khi gặp scope sở hữu class hoặc hết cây.
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

    // Kiểm tra quan hệ kế thừa giữa hai class symbol trong semantic model; hàm
    // đi theo chuỗi `superclass` và chặn chu trình để dùng an toàn khi xét protected.
    bool isSubclassSymbol(SymbolId derived, SymbolId base) const noexcept {
        if (derived == kInvalidSymbolId || base == kInvalidSymbolId) return false;
        std::unordered_set<SymbolId> visited;
        for (SymbolId current = derived;
             current != kInvalidSymbolId && visited.insert(current).second;
             current = model_.symbols[current].superclass) {
            if (current == base) return true;
        }
        return false;
    }

    // Tìm method symbol theo class hierarchy; bắt đầu từ lớp được chỉ định rồi
    // đi lên superclass, giống thứ tự runtime dispatch nhưng chỉ dùng metadata semantic.
    SymbolId lookupMethodSymbol(SymbolId classSymbol,
                                const std::string &memberName) const noexcept {
        std::unordered_set<SymbolId> visited;
        for (SymbolId current = classSymbol;
             current != kInvalidSymbolId && visited.insert(current).second;
             current = model_.symbols[current].superclass) {
            if (current >= model_.symbols.size()) return kInvalidSymbolId;
            const ScopeId memberScope = model_.symbols[current].memberScope;
            if (memberScope == kInvalidScopeId || memberScope >= bindings_.size()) continue;
            const SymbolId member = symbolInScope(
                memberScope, SymbolSpace::Value, memberName);
            if (member != kInvalidSymbolId &&
                model_.symbols[member].kind == SemanticSymbolKind::Method) {
                return member;
            }
        }
        return kInvalidSymbolId;
    }

    // Kiểm tra điều kiện của `validateMemberAccess`.
    void validateMemberAccess(const SemanticSymbol &symbol,
                              ScopeId useScope,
                              SourceSpan useSpan) {
        if (symbol.kind != SemanticSymbolKind::Method ||
            symbol.visibility == SemanticVisibility::Public ||
            symbol.visibility == SemanticVisibility::Unspecified) {
            return;
        }

        const SymbolId callerClass = enclosingClass(useScope);
        if (callerClass == symbol.ownerClass) return;
        if (symbol.visibility == SemanticVisibility::Protected &&
            isSubclassSymbol(callerClass, symbol.ownerClass)) {
            return;
        }

        const auto &definition = symbol.visibility == SemanticVisibility::Private
            ? vietvm::messages::kSemanticPrivateMethodAccess
            : vietvm::messages::kSemanticProtectedMethodAccess;
        model_.diagnostics.push_back({
            SemanticDiagnosticSeverity::Error,
            vietvm::messages::messageText(definition, {symbol.qualifiedName}),
            useSpan,
        });
    }

    // Suy luận lớp của một expression sau khi expression đó đã được resolve; class
    // constructor cho lớp trực tiếp, còn name kế thừa metadata từ symbol nguồn.
    SymbolId inferredClassForExpression(ExprId id) const noexcept {
        const AstExpression *expression = program_.expression(id);
        if (expression == nullptr) return kInvalidSymbolId;
        if (expression->kind == AstExpressionKind::Call &&
            id < model_.callBindings.size()) {
            const CallBinding &call = model_.callBindings[id];
            if (call.kind == CallTargetKind::ClassConstructor &&
                call.symbol != kInvalidSymbolId) {
                return call.symbol;
            }
        }
        if (expression->kind == AstExpressionKind::Name &&
            id < model_.expressionBindings.size()) {
            const BindingResult &binding = model_.expressionBindings[id];
            // Chỉ alias trực tiếp của một symbol mới kế thừa inferred class. Member/field
            // là động trong V++ 1.0; `x = mình.field` không đủ dữ liệu để suy ra lớp của x.
            if (binding.kind == BindingKind::Symbol &&
                binding.symbol != kInvalidSymbolId &&
                binding.symbol < model_.symbols.size()) {
                return model_.symbols[binding.symbol].inferredClass;
            }
        }
        return kInvalidSymbolId;
    }

    // Ghi lớp instance cho target của phép gán đơn; phép gán constructor hoặc alias
    // cập nhật metadata, còn gán giá trị khác xóa suy luận cũ để tránh diagnostic sai.
    void updateAssignedInstanceClass(const AstExpression &expression) {
        if (expression.kind != AstExpressionKind::Assignment) return;
        const AstExpression *left = program_.expression(expression.left);
        if (left == nullptr || left->kind != AstExpressionKind::Name ||
            !splitQualifiedRuntimeMember(left->text).first.empty() ||
            expression.left >= model_.expressionBindings.size()) {
            return;
        }
        const BindingResult &binding = model_.expressionBindings[expression.left];
        if (binding.symbol == kInvalidSymbolId ||
            binding.symbol >= model_.symbols.size()) {
            return;
        }
        if (ambiguousInstanceClasses_.find(binding.symbol) !=
            ambiguousInstanceClasses_.end()) {
            return;
        }
        const SymbolId inferred = inferredClassForExpression(expression.right);
        SymbolId &current = model_.symbols[binding.symbol].inferredClass;
        if (current == kInvalidSymbolId) {
            if (inferred != kInvalidSymbolId) current = inferred;
            return;
        }
        if (inferred == current) return;

        // Một symbol nhận nhiều loại giá trị không còn có lớp tĩnh đáng tin cậy.
        // Từ đây semantic ngừng suy luận member visibility cho symbol đó và để VM
        // thực hiện access check động, tránh diagnostic sai ở các nhánh điều khiển.
        current = kInvalidSymbolId;
        ambiguousInstanceClasses_.insert(binding.symbol);
    }

    // Kiểm tra điều kiện của `isKnownNative`.
    bool isKnownNative(const std::string &name) const {
        return std::find(environment_.nativeCallables.begin(),
                         environment_.nativeCallables.end(), name) !=
               environment_.nativeCallables.end();
    }

    // Trả tên văn bản ổn định cho bind; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
    BindingResult bindName(const AstExpression &expression, ScopeId scope, bool callableUse) {
        BindingResult binding;
        binding.expression = expression.id;
        binding.lookupScope = scope;
        binding.runtimeName = expression.text;
        LookupResult found = lookup(expression.text, scope);
        if (found.symbol == kInvalidSymbolId && callableUse) {
            const LookupResult type = lookupTypeLexical(expression.text, scope);
            if (type.symbol != kInvalidSymbolId &&
                model_.symbols[type.symbol].kind == SemanticSymbolKind::Class) {
                found = type;
            }
        }
        if (found.symbol != kInvalidSymbolId) {
            binding.kind = BindingKind::Symbol;
            binding.symbol = found.symbol;
            binding.lookupScope = found.scope;
            binding.lexicalDepth = found.depth;
            binding.captured = isCapturableKind(model_.symbols[found.symbol].kind) &&
                               crossesCallableBoundary(
                                   scope, model_.symbols[found.symbol].declaringScope);
            if (binding.captured) {
                recordLambdaCaptures(scope, model_.symbols[found.symbol]);
            }
            validateMemberAccess(model_.symbols[found.symbol], scope, expression.span);
        } else {
            const auto member = splitQualifiedRuntimeMember(expression.text);
            if (!member.first.empty()) {
                const LookupResult receiver = lookupLexical(member.first, scope);
                if (receiver.symbol != kInvalidSymbolId &&
                    isIndirectCallableKind(model_.symbols[receiver.symbol].kind)) {
                    binding.kind = BindingKind::InstanceMember;
                    binding.symbol = receiver.symbol;
                    binding.lookupScope = receiver.scope;
                    binding.lexicalDepth = receiver.depth;
                    binding.receiverName = member.first;
                    binding.memberName = member.second;
                    SymbolId receiverClass = model_.symbols[receiver.symbol].inferredClass;
                    if (member.first == "gốc" && receiverClass != kInvalidSymbolId &&
                        receiverClass < model_.symbols.size()) {
                        receiverClass = model_.symbols[receiverClass].superclass;
                    }
                    const SymbolId method = lookupMethodSymbol(receiverClass, member.second);
                    if (method != kInvalidSymbolId) {
                        validateMemberAccess(model_.symbols[method], scope, expression.span);
                    }
                    model_.expressionBindings[expression.id] = binding;
                    return binding;
                }
            }

            if (callableUse && isKnownNative(expression.text)) {
                binding.kind = BindingKind::NativeCallable;
            } else if (callableUse && policy_ == ResolutionPolicy::PreserveLegacy) {
                const auto hidden = std::find_if(
                    environment_.hiddenImportedSymbols.begin(),
                    environment_.hiddenImportedSymbols.end(),
                    [&](const SemanticHiddenImportedSymbol &symbol) {
                        return symbol.name == expression.text;
                    });
                if (hidden != environment_.hiddenImportedSymbols.end()) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticModuleSymbolNotExported,
                            {expression.text, hidden->moduleIdentity}),
                        expression.span,
                    });
                } else {
                    binding.kind = BindingKind::DynamicName;
                }
            } else if (!callableUse && policy_ == ResolutionPolicy::PreserveLegacy) {
                const auto hidden = std::find_if(
                    environment_.hiddenImportedSymbols.begin(),
                    environment_.hiddenImportedSymbols.end(),
                    [&](const SemanticHiddenImportedSymbol &symbol) {
                        return symbol.name == expression.text;
                    });
                if (hidden != environment_.hiddenImportedSymbols.end()) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticModuleSymbolNotExported,
                            {expression.text, hidden->moduleIdentity}),
                        expression.span,
                    });
                } else {
                    binding.kind = BindingKind::LegacyImplicitValue;
                }
            } else {
                const auto &definition = callableUse
                    ? vietvm::messages::kSemanticUnresolvedCall
                    : vietvm::messages::kSemanticUnresolvedName;
                model_.diagnostics.push_back({
                    SemanticDiagnosticSeverity::Error,
                    vietvm::messages::messageText(definition, {expression.text}),
                    expression.span,
                });
            }
        }
        model_.expressionBindings[expression.id] = binding;
        return binding;
    }

    // Kiểm tra arity khi semantic biết chắc callable đích. V++ vẫn dynamic typing:
    // kiểm tra này chỉ khóa call boundary về số đối số, không suy luận kiểu đối số.
    void validateKnownCallArity(const AstExpression &call,
                                const AstExpression *callee,
                                const BindingResult &calleeBinding,
                                const CallBinding &binding) {
        const SemanticSymbol *callable = nullptr;
        std::string displayName = callee == nullptr ? binding.runtimeName : callee->text;

        if (binding.kind == CallTargetKind::DirectFunction &&
            binding.symbol != kInvalidSymbolId && binding.symbol < model_.symbols.size()) {
            const SemanticSymbol &candidate = model_.symbols[binding.symbol];
            if (candidate.origin == SymbolOrigin::Source) callable = &candidate;
        } else if (binding.kind == CallTargetKind::ClassConstructor &&
                   binding.symbol != kInvalidSymbolId && binding.symbol < model_.symbols.size()) {
            const SemanticSymbol &klass = model_.symbols[binding.symbol];
            // Imported class symbols hiện chưa mang member/constructor metadata qua
            // SemanticEnvironment; runtime linker sẽ kiểm tra arity sau khi resolve class.
            if (klass.origin != SymbolOrigin::Source) return;
            displayName = klass.lookupName;
            const SymbolId constructor = klass.memberScope == kInvalidScopeId
                ? kInvalidSymbolId
                : symbolInScope(klass.memberScope, SymbolSpace::Value, "khởi tạo");
            if (constructor == kInvalidSymbolId) {
                if (!call.arguments.empty()) {
                    model_.diagnostics.push_back({
                        SemanticDiagnosticSeverity::Error,
                        vietvm::messages::messageText(
                            vietvm::messages::kSemanticCallArityMismatch,
                            {displayName, std::to_string(call.arguments.size()), "0", "0"}),
                        call.span,
                    });
                }
                return;
            }
            if (constructor < model_.symbols.size()) callable = &model_.symbols[constructor];
        } else if (binding.kind == CallTargetKind::InstanceMethod &&
                   calleeBinding.kind == BindingKind::InstanceMember &&
                   calleeBinding.symbol != kInvalidSymbolId &&
                   calleeBinding.symbol < model_.symbols.size()) {
            SymbolId receiverClass = model_.symbols[calleeBinding.symbol].inferredClass;
            if (calleeBinding.receiverName == "gốc" && receiverClass != kInvalidSymbolId &&
                receiverClass < model_.symbols.size()) {
                receiverClass = model_.symbols[receiverClass].superclass;
            }
            const SymbolId method = lookupMethodSymbol(receiverClass, calleeBinding.memberName);
            if (method != kInvalidSymbolId && method < model_.symbols.size()) {
                callable = &model_.symbols[method];
                displayName = calleeBinding.memberName;
            }
        }

        if (callable == nullptr || callable->origin != SymbolOrigin::Source) return;
        const std::size_t actual = call.arguments.size();
        if (actual >= callable->minimumArgumentCount && actual <= callable->parameterCount) return;
        model_.diagnostics.push_back({
            SemanticDiagnosticSeverity::Error,
            vietvm::messages::messageText(
                vietvm::messages::kSemanticCallArityMismatch,
                {displayName, std::to_string(actual),
                 std::to_string(callable->minimumArgumentCount),
                 std::to_string(callable->parameterCount)}),
            call.span,
        });
    }

    // Ghi binding của call expression sau khi phân giải callee; hàm phân loại direct/imported/method/native/dynamic và lưu metadata dispatch.
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
        } else if (calleeBinding.kind == BindingKind::InstanceMember) {
            result.kind = CallTargetKind::InstanceMethod;
        } else if (calleeBinding.kind == BindingKind::DynamicName) {
            result.kind = CallTargetKind::DynamicName;
        } else if (calleeBinding.kind == BindingKind::Symbol) {
            const SemanticSymbol &symbol = model_.symbols[calleeBinding.symbol];
            if (symbol.kind == SemanticSymbolKind::Class) {
                result.kind = CallTargetKind::ClassConstructor;
            } else if (symbol.origin == SymbolOrigin::Imported && isCallableKind(symbol.kind)) {
                // Imported functions have a known semantic identity but no VM
                // function ID in this module's predeclaration table. Keep them
                // on the name-based call path until module linking owns IDs.
                result.kind = CallTargetKind::ImportedFunction;
            } else {
                const SemanticSymbolKind kind = symbol.kind;
                result.kind = isCallableKind(kind)
                    ? CallTargetKind::DirectFunction
                    : (isIndirectCallableKind(kind) ? CallTargetKind::IndirectValue
                                                    : CallTargetKind::Invalid);
            }
        }
        model_.callBindings[call.id] = result;
        validateKnownCallArity(call, callee, calleeBinding, result);

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

    // Phân giải biểu thức; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
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
            case AstExpressionKind::CompoundAssignment:
                resolveExpression(expression->left, scope);
                resolveExpression(expression->right, scope);
                break;
            case AstExpressionKind::Assignment:
                resolveExpression(expression->left, scope);
                resolveExpression(expression->right, scope);
                updateAssignedInstanceClass(*expression);
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
                {
                    const ScopeId lambdaScope = ensureLambdaBody(*expression, scope);
                    const AstLambda *lambda = program_.lambda(expression->lambdaId);
                    if (lambda != nullptr && lambda->expression == expression->id &&
                        resolvedLambdaBodies_.insert(expression->id).second) {
                        for (const auto &parameter : lambda->parameters) {
                            resolveExpression(parameter.defaultValue, lambdaScope);
                        }
                        resolveStatementList(lambda->body.children);
                    }
                }
                break;
            case AstExpressionKind::MapLiteral:
                for (const auto &entry : expression->mapEntries) {
                    resolveExpression(entry.value, scope);
                }
                break;
            case AstExpressionKind::ListLiteral:
                for (ExprId element : expression->listElements) {
                    resolveExpression(element, scope);
                }
                break;
            case AstExpressionKind::Index:
                resolveExpression(expression->left, scope);
                resolveExpression(expression->right, scope);
                break;
        }
    }

    // Phân giải fallback calls; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
    void resolveFallbackCalls(const AstStatement &statement, ScopeId scope) {
        const bool mayHavePartialRoots = statement.kind == AstStatementKind::Loop;
        if ((!statement.expressionRoots.empty() && !mayHavePartialRoots) ||
            statement.kind == AstStatementKind::Function ||
            statement.kind == AstStatementKind::Class ||
            statement.kind == AstStatementKind::Interface ||
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

    // Phân giải câu lệnh danh sách; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
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
    std::unordered_map<ExprId, std::size_t> lambdaModelIndices_;
    std::unordered_map<ScopeId, std::size_t> lambdaScopeModels_;
    std::unordered_set<ExprId> builtLambdaBodies_;
    std::unordered_set<ExprId> declaredLambdaBodies_;
    std::unordered_set<ExprId> resolvedLambdaBodies_;
    std::unordered_set<SymbolId> ambiguousInstanceClasses_;
    std::unordered_map<std::string, SymbolId> qualifiedValues_;
};

} // namespace

// Kiểm tra điều kiện của `hasErrors`.
bool SemanticModel::hasErrors() const noexcept {
    for (const SemanticDiagnostic &diagnostic : diagnostics) {
        if (diagnostic.severity == SemanticDiagnosticSeverity::Error) return true;
    }
    return false;
}

// Tra `SemanticSymbol` tương ứng với một statement khai báo; hàm dùng bảng `declarationSymbols` đã được analyzer tạo thay vì tìm lại theo tên.
int SemanticModel::symbolForDeclaration(std::size_t tokenBegin) const noexcept {
    const auto found = declarationSymbols.find(tokenBegin);
    return found == declarationSymbols.end() ? -1 : found->second;
}

// Tra semantic scope gắn với một statement; hàm dùng token-begin/key ổn định để trả scope mà analyzer đã xây.
ScopeId SemanticModel::scopeForStatement(std::size_t tokenBegin) const noexcept {
    const auto found = statementScopes.find(tokenBegin);
    return found == statementScopes.end() ? kInvalidScopeId : found->second;
}

// Tra semantic scope nơi một expression được phân tích; hàm đọc bảng `expressionScopes` theo ExprId.
ScopeId SemanticModel::scopeForExpression(ExprId expression) const noexcept {
    return expression < expressionScopes.size() ? expressionScopes[expression]
                                                : kInvalidScopeId;
}

// Tra kết quả binding của một expression; hàm trả symbol/member/native metadata mà semantic analyzer đã ghi cho ExprId đó.
const BindingResult *SemanticModel::bindingForExpression(ExprId expression) const noexcept {
    return expression < expressionBindings.size() ? &expressionBindings[expression] : nullptr;
}

// Tra metadata đích gọi của một call expression; hàm trả loại dispatch và symbol/runtime name đã được semantic phân giải.
const CallBinding *SemanticModel::callBindingForExpression(ExprId expression) const noexcept {
    if (expression >= callBindings.size() ||
        callBindings[expression].kind == CallTargetKind::Invalid) {
        return nullptr;
    }
    return &callBindings[expression];
}

// Tra `SemanticLambda` tương ứng với expression lambda; hàm ánh xạ ExprId sang record capture/scope đã dựng.
const SemanticLambda *SemanticModel::lambdaForExpression(ExprId expression) const noexcept {
    const auto found = std::find_if(
        lambdas.begin(), lambdas.end(),
        [expression](const SemanticLambda &lambda) {
            return lambda.expression == expression;
        });
    return found == lambdas.end() ? nullptr : &*found;
}

// Xây dựng `SemanticModel` từ AST; analyzer tạo scope/symbol, phân giải tên và ghi lại binding cùng diagnostic.
SemanticModel analyzeSemantics(const AstProgram &program) {
    return analyzeSemantics(program, SemanticEnvironment{}, ResolutionPolicy::PreserveLegacy);
}

// Xây dựng `SemanticModel` từ AST; analyzer tạo scope/symbol, phân giải tên và ghi lại binding cùng diagnostic.
SemanticModel analyzeSemantics(const AstProgram &program,
                               const SemanticEnvironment &environment,
                               ResolutionPolicy policy) {
    return Analyzer(program, environment, policy).run();
}

// Trả tên ổn định của loại đích gọi semantic để tooling và test nhận biết cách một call sẽ được dispatch.
const char *callTargetKindName(CallTargetKind kind) noexcept {
    switch (kind) {
        case CallTargetKind::Invalid: return "invalid";
        case CallTargetKind::DirectFunction: return "direct_function";
        case CallTargetKind::ImportedFunction: return "imported_function";
        case CallTargetKind::ClassConstructor: return "class_constructor";
        case CallTargetKind::InstanceMethod: return "instance_method";
        case CallTargetKind::IndirectValue: return "indirect_value";
        case CallTargetKind::Native: return "native";
        case CallTargetKind::DynamicName: return "dynamic_name";
    }
    return "invalid";
}

} // namespace vietvm::compiler
