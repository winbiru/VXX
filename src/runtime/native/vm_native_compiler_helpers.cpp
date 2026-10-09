#include "common/vm_native_compiler_helpers.h"

#include <filesystem>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/vm_native_helpers.h"
#include "common/vm_native_stdlib_helpers.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/core/text.h"
#include "vpp/frontend/parser.h"
#include "vm/vm.h"

namespace vietvm::helpers {
namespace {

bool requireSource(const StackValue &value,
                   const std::string &fn,
                   std::string &source,
                   std::string &err) {
    if (!std::holds_alternative<std::string>(value)) {
        err = fn + ": mã nguồn phải là chuỗi";
        return false;
    }
    source = std::get<std::string>(value);
    if (!vietvm::core::isValidUtf8(source)) {
        err = fn + ": mã nguồn không phải UTF-8 hợp lệ";
        return false;
    }
    return true;
}

vietvm::compiler::CompilationArtifacts compileText(
    const std::string &source,
    const std::filesystem::path &base) {
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = base;
    return vietvm::compiler::compilePipeline(context, source, keywordMap, false);
}

StackValue bytecodePrimitiveList(const vietvm::compiler::CompilationArtifacts &artifacts) {
    std::vector<StackValue> instructions;
    instructions.reserve(artifacts.bytecode.size());
    for (const Instruction &instruction : artifacts.bytecode) {
        instructions.push_back(make_list_value({
            make_int_value(instruction.op),
            make_int_value(instruction.operand),
            make_int_value(instruction.operandIndex),
            make_int_value(instruction.operandValue),
        }));
    }
    return make_list_value(std::move(instructions));
}

vietvm::frontend::AstProgram parseSourceAst(const std::string &source) {
    return vietvm::frontend::parseTokens(
        vietvm::compiler::postProcessTokensWithSpans(
            vietvm::compiler::tokenizeWithSpans(source)));
}

int visibilityCode(vietvm::frontend::AstVisibility visibility) {
    using vietvm::frontend::AstVisibility;
    switch (visibility) {
        case AstVisibility::Unspecified: return 0;
        case AstVisibility::Public: return 1;
        case AstVisibility::Private: return 2;
        case AstVisibility::Protected: return 3;
    }
    return 0;
}

StackValue astParametersPrimitive(const vietvm::frontend::AstStatement &statement) {
    std::vector<StackValue> parameters;
    parameters.reserve(statement.parameters.size());
    for (const auto &parameter : statement.parameters) {
        parameters.push_back(make_list_value({
            make_string_value(parameter.name),
            make_int_value(parameter.hasDefault ? 1 : 0),
        }));
    }
    return make_list_value(std::move(parameters));
}

StackValue astStatementPrimitive(const vietvm::frontend::AstStatement &statement) {
    using vietvm::frontend::AstStatementKind;
    int kindCode = 4;
    if (statement.kind == AstStatementKind::Import) kindCode = 0;
    else if (statement.kind == AstStatementKind::Function) kindCode = 1;
    else if (statement.kind == AstStatementKind::Class) kindCode = 2;
    else if (statement.kind == AstStatementKind::Interface) kindCode = 3;

    std::vector<StackValue> children;
    children.reserve(statement.children.size());
    for (const auto &child : statement.children) {
        children.push_back(astStatementPrimitive(child));
    }
    return make_list_value({
        make_int_value(kindCode),
        make_string_value(statement.declarationName),
        make_int_value(visibilityCode(statement.visibility)),
        astParametersPrimitive(statement),
        make_string_value(statement.importSpec.target),
        make_string_value(statement.importSpec.alias),
        make_int_value(statement.importSpec.reExport ? 1 : 0),
        make_list_value(std::move(children)),
    });
}

StackValue astStatementsPrimitive(const vietvm::frontend::AstProgram &program) {
    std::vector<StackValue> statements;
    statements.reserve(program.statements.size());
    for (const auto &statement : program.statements) {
        statements.push_back(astStatementPrimitive(statement));
    }
    return make_list_value(std::move(statements));
}

StackValue compilationAnalysisPrimitive(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    const vietvm::frontend::AstProgram &program) {
    // Native compiler chỉ phơi payload thô. Trạng thái thành công/thất bại,
    // tên trường, chuỗi loại AST và contract public do V++ dựng.
    return make_list_value({
        make_int_value(static_cast<int>(artifacts.tokens.size())),
        make_int_value(static_cast<int>(artifacts.semantic.symbols.size())),
        make_int_value(static_cast<int>(artifacts.semantic.scopes.size())),
        make_int_value(static_cast<int>(artifacts.semantic.references.size())),
        make_int_value(static_cast<int>(artifacts.bytecode.size())),
        make_int_value(static_cast<int>(artifacts.unsupportedDirectIrRegions)),
        make_int_value(artifacts.moduleIndex.has_value()
            ? static_cast<int>(artifacts.moduleIndex->graph.modules.size())
            : 0),
        bytecodePrimitiveList(artifacts),
        astStatementsPrimitive(program),
    });
}

std::string executeEmbeddedSource(const std::string &source,
                                  const std::filesystem::path &base) {
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = base;
    context.currentSourceIdentity = "<embedded>";
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, source, keywordMap, true);

    VM vm(artifacts.bytecode, context.stringPool);
    std::string output;
    vm.setOutputSink([&output](const std::string &text) { output += text; });

    std::unordered_map<int, int> functionNames;
    for (const auto &entry : context.functionNameIndices) {
        functionNames[entry.second] = entry.first;
    }
    vm.setFunctions(context.functionBytecode, std::move(functionNames));
    vm.setDebugInfo(artifacts.bytecodeDebugInfo, context.functionDebugInfo);
    for (const auto &module : context.moduleInitializers) {
        (void)vm.addModuleInitializer(
            module.identity, module.bytecode, module.debugInfo);
    }
    vm.run();
    return output;
}

} // namespace

bool embeddedVmRunPrimitive(const std::vector<StackValue> &args,
                            StackValue &result,
                            std::string &err) {
    constexpr const char *fn = "kich_ban_chay_vm";
    if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
    std::string source;
    std::filesystem::path base;
    if (!requireSource(args[0], fn, source, err)) return true;
    if (!nativeUtf8Path(args[1], fn, base, err)) return true;
    try {
        result = make_string_value(executeEmbeddedSource(source, base));
    } catch (const std::exception &e) {
        err = e.what();
    }
    return true;
}

bool compilerAnalyzePrimitive(const std::vector<StackValue> &args,
                              StackValue &result,
                              std::string &err) {
    constexpr const char *fn = "bien_dich_phan_tich_vm";
    if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
    std::string source;
    std::filesystem::path base;
    if (!requireSource(args[0], fn, source, err)) return true;
    if (!nativeUtf8Path(args[1], fn, base, err)) return true;
    try {
        result = compilationAnalysisPrimitive(
            compileText(source, base), parseSourceAst(source));
    } catch (const std::exception &e) {
        // Compiler primitive chỉ báo lỗi thô. V++ quyết định lỗi này được
        // chuyển thành snapshot hay được ném ra ngoài.
        err = e.what();
    }
    return true;
}

} // namespace vietvm::helpers
