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

StackValue compilationSummary(const vietvm::compiler::CompilationArtifacts &artifacts) {
    MapValue summary;
    summary.entries["token"] = make_int_value(static_cast<int>(artifacts.tokens.size()));
    summary.entries["ký hiệu"] = make_int_value(static_cast<int>(artifacts.semantic.symbols.size()));
    summary.entries["phạm vi"] = make_int_value(static_cast<int>(artifacts.semantic.scopes.size()));
    summary.entries["tham chiếu"] = make_int_value(static_cast<int>(artifacts.semantic.references.size()));
    summary.entries["lệnh"] = make_int_value(static_cast<int>(artifacts.bytecode.size()));
    summary.entries["vùng ir chưa trực tiếp"] =
        make_int_value(static_cast<int>(artifacts.unsupportedDirectIrRegions));
    summary.entries["mô đun"] = make_int_value(
        artifacts.moduleIndex.has_value()
            ? static_cast<int>(artifacts.moduleIndex->graph.modules.size())
            : 0);
    return make_map_value(std::move(summary));
}

StackValue bytecodeList(const vietvm::compiler::CompilationArtifacts &artifacts) {
    std::vector<StackValue> instructions;
    instructions.reserve(artifacts.bytecode.size());
    for (const Instruction &instruction : artifacts.bytecode) {
        MapValue row;
        row.entries["op"] = make_int_value(instruction.op);
        row.entries["toán hạng"] = make_int_value(instruction.operand);
        row.entries["chỉ số"] = make_int_value(instruction.operandIndex);
        row.entries["giá trị"] = make_int_value(instruction.operandValue);
        instructions.push_back(make_map_value(std::move(row)));
    }
    return make_list_value(std::move(instructions));
}

vietvm::frontend::AstProgram parseSourceAst(const std::string &source) {
    return vietvm::frontend::parseTokens(
        vietvm::compiler::postProcessTokensWithSpans(
            vietvm::compiler::tokenizeWithSpans(source)));
}

const char *visibilityText(vietvm::frontend::AstVisibility visibility) {
    using vietvm::frontend::AstVisibility;
    switch (visibility) {
        case AstVisibility::Public: return "công khai";
        case AstVisibility::Private: return "riêng tư";
        case AstVisibility::Protected: return "bảo vệ";
        case AstVisibility::Unspecified: return "mặc định";
    }
    return "mặc định";
}

StackValue astParameters(const vietvm::frontend::AstStatement &statement) {
    std::vector<StackValue> parameters;
    parameters.reserve(statement.parameters.size());
    for (const auto &parameter : statement.parameters) {
        MapValue row;
        row.entries["tên"] = make_string_value(parameter.name);
        row.entries["có mặc định"] = make_int_value(parameter.hasDefault ? 1 : 0);
        parameters.push_back(make_map_value(std::move(row)));
    }
    return make_list_value(std::move(parameters));
}

StackValue astStatementValue(const vietvm::frontend::AstStatement &statement) {
    using vietvm::frontend::AstStatementKind;
    MapValue row;
    const char *kind = "khác";
    if (statement.kind == AstStatementKind::Import) kind = "nhập";
    else if (statement.kind == AstStatementKind::Function) kind = "hàm";
    else if (statement.kind == AstStatementKind::Class) kind = "lớp";
    else if (statement.kind == AstStatementKind::Interface) kind = "giao diện";
    row.entries["loại"] = make_string_value(kind);
    row.entries["tên"] = make_string_value(statement.declarationName);
    row.entries["phạm vi"] = make_string_value(visibilityText(statement.visibility));
    row.entries["tham số"] = astParameters(statement);
    row.entries["đích"] = make_string_value(statement.importSpec.target);
    row.entries["bí danh"] = make_string_value(statement.importSpec.alias);
    row.entries["công khai"] = make_int_value(statement.importSpec.reExport ? 1 : 0);

    std::vector<StackValue> children;
    children.reserve(statement.children.size());
    for (const auto &child : statement.children) {
        children.push_back(astStatementValue(child));
    }
    row.entries["thành viên"] = make_list_value(std::move(children));
    return make_map_value(std::move(row));
}

StackValue astStatements(const vietvm::frontend::AstProgram &program) {
    std::vector<StackValue> statements;
    statements.reserve(program.statements.size());
    for (const auto &statement : program.statements) {
        statements.push_back(astStatementValue(statement));
    }
    return make_list_value(std::move(statements));
}

StackValue compilationAnalysis(const vietvm::compiler::CompilationArtifacts &artifacts,
                               const vietvm::frontend::AstProgram &program) {
    StackValue summary = compilationSummary(artifacts);
    MapHandle map = std::get<MapHandle>(summary);
    map->entries["cây cú pháp"] = astStatements(program);
    return summary;
}

std::string executeEmbeddedSource(const std::string &source,
                                  const std::filesystem::path &base) {
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = base.empty()
        ? std::filesystem::current_path()
        : base;
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

bool handleNativeCompilerLibraryFunction(const std::string &fn,
                                         const std::vector<StackValue> &args,
                                         StackValue &result,
                                         std::string &err) {
    if (fn == "phu_thuoc_kiem_tra_noi_bo") {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::string source;
        std::filesystem::path base;
        if (!requireSource(args[0], fn, source, err)) return true;
        if (!nativeUtf8Path(args[1], fn, base, err)) return true;
        if (base.empty()) base = std::filesystem::current_path();
        MapValue check;
        try {
            (void)compileText(source, base);
            check.entries["hợp lệ"] = make_int_value(1);
            check.entries["lỗi"] = make_string_value("");
        } catch (const std::exception &e) {
            check.entries["hợp lệ"] = make_int_value(0);
            check.entries["lỗi"] = make_string_value(e.what());
        }
        result = make_map_value(std::move(check));
        return true;
    }

    if (fn == "kich_ban_chay_noi_bo") {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::string source;
        std::filesystem::path base;
        if (!requireSource(args[0], fn, source, err)) return true;
        if (!nativeUtf8Path(args[1], fn, base, err)) return true;
        if (base.empty()) base = std::filesystem::current_path();
        try {
            result = make_string_value(executeEmbeddedSource(source, base));
        } catch (const std::exception &e) {
            err = fn + ": " + e.what();
        }
        return true;
    }

    if (fn == "bien_dich_kiem_tra") {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::string source;
        if (!requireSource(args[0], fn, source, err)) return true;
        MapValue check;
        try {
            const auto artifacts = compileText(source, std::filesystem::current_path());
            check.entries["hợp lệ"] = make_int_value(1);
            check.entries["lỗi"] = make_string_value("");
            check.entries["lệnh"] = make_int_value(static_cast<int>(artifacts.bytecode.size()));
        } catch (const std::exception &e) {
            check.entries["hợp lệ"] = make_int_value(0);
            check.entries["lỗi"] = make_string_value(e.what());
            check.entries["lệnh"] = make_int_value(0);
        }
        result = make_map_value(std::move(check));
        return true;
    }

    if (fn == "bien_dich_bytecode") {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::string source;
        if (!requireSource(args[0], fn, source, err)) return true;
        try {
            const auto artifacts = compileText(source, std::filesystem::current_path());
            result = bytecodeList(artifacts);
        } catch (const std::exception &e) {
            err = fn + ": " + e.what();
        }
        return true;
    }

    if (fn == "bien_dich_phan_tich_noi_bo") {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::string source;
        std::filesystem::path base;
        if (!requireSource(args[0], fn, source, err)) return true;
        if (!nativeUtf8Path(args[1], fn, base, err)) return true;
        if (base.empty()) base = std::filesystem::current_path();
        try {
            result = compilationAnalysis(compileText(source, base), parseSourceAst(source));
        } catch (const std::exception &e) {
            err = fn + ": " + e.what();
        }
        return true;
    }

    return false;
}

} // namespace vietvm::helpers
