#include "common/vm_native_compiler_helpers.h"

#include <filesystem>
#include <algorithm>
#include <stdexcept>
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

StackValue bytecodePrimitiveList(const std::vector<Instruction> &bytecode) {
    std::vector<StackValue> instructions;
    instructions.reserve(bytecode.size());
    for (const Instruction &instruction : bytecode) {
        instructions.push_back(make_list_value({
            make_int_value(instruction.op),
            make_int_value(instruction.operand),
            make_int_value(instruction.operandIndex),
            make_int_value(instruction.operandValue),
        }));
    }
    return make_list_value(std::move(instructions));
}

// Read-only oracle for checking self-hosted function emitters. Preserve the
// C++ registry's function IDs and four-field instruction ABI verbatim.
StackValue namedFunctionBytecodePrimitiveList(
    const vietvm::compiler::CompilationContext &context) {
    std::vector<int> ids;
    ids.reserve(context.functionNameIndices.size());
    for (const auto &[id, nameIndex] : context.functionNameIndices) {
        if (nameIndex >= 0 &&
            static_cast<std::size_t>(nameIndex) < context.stringPool.size() &&
            context.functionBytecode.count(id) != 0) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    std::vector<StackValue> functions;
    functions.reserve(ids.size());
    for (int id : ids) {
        const int nameIndex = context.functionNameIndices.at(id);
        functions.push_back(make_list_value({
            make_string_value(context.stringPool[static_cast<std::size_t>(nameIndex)]),
            make_int_value(id),
            bytecodePrimitiveList(context.functionBytecode.at(id)),
        }));
    }
    return make_list_value(std::move(functions));
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
    const vietvm::frontend::AstProgram &program,
    const vietvm::compiler::CompilationContext &context) {
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
        bytecodePrimitiveList(artifacts.bytecode),
        astStatementsPrimitive(program),
        namedFunctionBytecodePrimitiveList(context),
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

const std::vector<StackValue> &requireWireList(const StackValue &value,
                                               const std::string &where) {
    if (!std::holds_alternative<ListHandle>(value) ||
        std::get<ListHandle>(value) == nullptr) {
        throw std::runtime_error("bytecode_chay_vm: " + where + " phải là danh sách");
    }
    return std::get<ListHandle>(value)->elements;
}

int requireWireInt(const StackValue &value, const std::string &where) {
    if (!std::holds_alternative<int>(value)) {
        throw std::runtime_error("bytecode_chay_vm: " + where + " phải là số nguyên i32");
    }
    return std::get<int>(value);
}

const std::string &requireWireString(const StackValue &value,
                                     const std::string &where) {
    if (!std::holds_alternative<std::string>(value)) {
        throw std::runtime_error("bytecode_chay_vm: " + where + " phải là chuỗi");
    }
    return std::get<std::string>(value);
}

std::vector<Instruction> loadWireInstructions(const StackValue &value,
                                               const std::string &where) {
    const auto &records = requireWireList(value, where);
    std::vector<Instruction> code;
    code.reserve(records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto &record = requireWireList(records[i], where + "[" + std::to_string(i) + "]");
        if (record.size() != 4) {
            throw std::runtime_error("bytecode_chay_vm: lệnh phải có đúng 4 trường ABI tại " + where);
        }
        const std::string prefix = where + "[" + std::to_string(i) + "]";
        code.push_back(Instruction{
            requireWireInt(record[0], prefix + ".op"),
            requireWireInt(record[1], prefix + ".toán hạng"),
            requireWireInt(record[2], prefix + ".chỉ số"),
            requireWireInt(record[3], prefix + ".giá trị"),
        });
    }
    return code;
}

std::string executeLinkedBytecode(const std::vector<StackValue> &args) {
    const auto root = loadWireInstructions(args[0], "bytecode gốc");

    std::vector<std::string> pool;
    const auto &strings = requireWireList(args[1], "StringPool");
    pool.reserve(strings.size());
    for (std::size_t i = 0; i < strings.size(); ++i) {
        pool.push_back(requireWireString(strings[i], "StringPool[" + std::to_string(i) + "]"));
    }

    std::unordered_map<int, std::vector<Instruction>> functionBytecode;
    std::unordered_map<int, int> functionNames;
    const auto &functions = requireWireList(args[2], "bảng hàm");
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const auto &row = requireWireList(functions[i], "bảng hàm[" + std::to_string(i) + "]");
        if (row.size() != 3) {
            throw std::runtime_error("bytecode_chay_vm: bản ghi hàm phải có 3 trường");
        }
        const int id = requireWireInt(row[0], "function ID");
        const int nameIndex = requireWireInt(row[1], "function runtime name index");
        if (id != static_cast<int>(i)) {
            throw std::runtime_error("bytecode_chay_vm: function ID phải liên tiếp từ 0");
        }
        if (nameIndex < 0 || static_cast<std::size_t>(nameIndex) >= pool.size()) {
            throw std::runtime_error("bytecode_chay_vm: function name index ngoài StringPool");
        }
        if (!functionNames.emplace(nameIndex, id).second) {
            throw std::runtime_error("bytecode_chay_vm: trùng function runtime name index");
        }
        functionBytecode.emplace(id, loadWireInstructions(row[2], "thân hàm " + std::to_string(id)));
    }

    VM vm(root, pool);
    vm.setFunctions(std::move(functionBytecode), std::move(functionNames));
    const auto &initializers = requireWireList(args[3], "initializer");
    for (std::size_t i = 0; i < initializers.size(); ++i) {
        const auto &row = requireWireList(initializers[i], "initializer[" + std::to_string(i) + "]");
        if (row.size() != 2) {
            throw std::runtime_error("bytecode_chay_vm: initializer phải có 2 trường");
        }
        const std::string &identity = requireWireString(row[0], "định danh initializer");
        if (identity.empty()) {
            throw std::runtime_error("bytecode_chay_vm: định danh initializer rỗng");
        }
        if (!vm.addModuleInitializer(identity, loadWireInstructions(row[1], "initializer " + identity))) {
            throw std::runtime_error("bytecode_chay_vm: trùng định danh initializer '" + identity + "'");
        }
    }
    std::string output;
    vm.setOutputSink([&output](const std::string &text) { output += text; });
    vm.run();
    return output;
}

} // namespace

bool linkedBytecodeRunPrimitive(const std::vector<StackValue> &args,
                                StackValue &result,
                                std::string &err) {
    constexpr const char *fn = "bytecode_chay_vm";
    if (!requireNativeArgumentCount(args, fn, 4, err)) return true;
    try {
        result = make_string_value(executeLinkedBytecode(args));
    } catch (const std::exception &e) {
        err = e.what();
    }
    return true;
}

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
        vietvm::compiler::CompilationContext context;
        context.importResolutionBase = base;
        auto artifacts = vietvm::compiler::compilePipeline(
            context, source, keywordMap, false);
        result = compilationAnalysisPrimitive(
            artifacts, parseSourceAst(source), context);
    } catch (const std::exception &e) {
        // Compiler primitive chỉ báo lỗi thô. V++ quyết định lỗi này được
        // chuyển thành snapshot hay được ném ra ngoài.
        err = e.what();
    }
    return true;
}

} // namespace vietvm::helpers
