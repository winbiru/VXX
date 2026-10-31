#include "vpp/native/vpp_machine_bridge.h"

#include <charconv>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontend/keywords.h"
#include "vpp/native/abi_symbol.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/native/aot.h"

namespace vietvm::native {
namespace {

std::int32_t checkedInt(std::size_t value, const char *label);

// The wire schema uses exactly the C++ enum spellings so adding an IR opcode
// cannot silently re-interpret an unsupported instruction in V++.
const char *sourceOpcode(compiler::IrOpcode op) {
    using O = compiler::IrOpcode;
    switch (op) {
        case O::NoOp: return "NoOp";
        case O::Block: return "Block";
        case O::Import: return "Import";
        case O::DefineFunction: return "DefineFunction";
        case O::DefineClass: return "DefineClass";
        case O::Conditional: return "Conditional";
        case O::Loop: return "Loop";
        case O::Switch: return "Switch";
        case O::Return: return "Return";
        case O::Print: return "Print";
        case O::Break: return "Break";
        case O::Continue: return "Continue";
        case O::Throw: return "Throw";
        case O::Try: return "Try";
        case O::DeclareVariable: return "DeclareVariable";
        case O::Statement: return "Statement";
    }
    throw std::runtime_error("compiler IrOpcode C++ chưa định nghĩa");
}

const char *sourceValueOpcode(compiler::IrValueOpcode op) {
    using O = compiler::IrValueOpcode;
    switch (op) {
        case O::UnsupportedDirectRegion: return "UnsupportedDirectRegion";
        case O::ConstInt: return "ConstInt";
        case O::ConstFloat: return "ConstFloat";
        case O::ConstString: return "ConstString";
        case O::ConstBool: return "ConstBool";
        case O::ConstNull: return "ConstNull";
        case O::MapLiteral: return "MapLiteral";
        case O::ListLiteral: return "ListLiteral";
        case O::Index: return "Index";
        case O::StoreIndex: return "StoreIndex";
        case O::LoadName: return "LoadName";
        case O::LoadProperty: return "LoadProperty";
        case O::StoreName: return "StoreName";
        case O::StoreProperty: return "StoreProperty";
        case O::Unary: return "Unary";
        case O::Binary: return "Binary";
        case O::Call: return "Call";
        case O::CallDynamic: return "CallDynamic";
        case O::Intrinsic: return "Intrinsic";
        case O::ForeignCall: return "ForeignCall";
        case O::Lambda: return "Lambda";
    }
    throw std::runtime_error("compiler IrValueOpcode C++ chưa định nghĩa");
}

std::string quoted(const std::string &input) {
    std::string result = "\"";
    for (const unsigned char c : input) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32 || c == 127)
                    throw std::runtime_error("IR chứa ký tự điều khiển không thể serialize");
                result += static_cast<char>(c);
        }
    }
    return result + '"';
}

void writeSourceInstruction(std::ostream &out, const compiler::IrInstruction &inst,
                            unsigned depth = 0) {
    if (depth >= 64) throw std::runtime_error("IR statement lồng quá 64 tầng");
    out << "{\"opcode\":" << quoted(sourceOpcode(inst.opcode))
        << ",\"symbolId\":" << inst.symbolId
        << ",\"name\":" << quoted(inst.opcode == compiler::IrOpcode::DefineFunction
                                   ? nativeAbiSymbol(inst.declarationName)
                                   : inst.declarationName)
        << ",\"conditionalForm\":"
        << quoted(inst.conditionalForm == frontend::AstConditionalForm::IfBlock ? "IfBlock" :
                  inst.conditionalForm == frontend::AstConditionalForm::IfElseBlocks ? "IfElseBlocks" : "Unstructured")
        << ",\"loopForm\":"
        << quoted(inst.loopForm == frontend::AstLoopForm::ForBlock ? "ForBlock" :
                  inst.loopForm == frontend::AstLoopForm::WhileBlock ? "WhileBlock" : "Unstructured")
        << ",\"unsupported\":" << (inst.unsupportedDirectRegion ? "đúng" : "sai")
        << ",\"parameters\":[";
    for (std::size_t i = 0; i < inst.parameters.size(); ++i) {
        if (i) out << ',';
        out << "{\"symbolId\":" << inst.parameters[i].symbolId
            << ",\"hasDefault\":" << (inst.parameters[i].hasDefault ? "đúng" : "sai") << '}';
    }
    out << "],\"roots\":[";
    for (std::size_t i = 0; i < inst.expressionRoots.size(); ++i) {
        if (i) out << ',';
        out << checkedInt(inst.expressionRoots[i], "IR value id");
    }
    out << "],\"children\":[";
    for (std::size_t i = 0; i < inst.children.size(); ++i) {
        if (i) out << ',';
        writeSourceInstruction(out, inst.children[i], depth + 1);
    }
    out << "]}";
}

const char *opcodeName(MachineOpcode op) {
    using Op = MachineOpcode;
    switch (op) {
        case Op::PushConstant: return "PushConstant";
        case Op::LoadSlot: return "LoadSlot";
        case Op::StoreSlot: return "StoreSlot";
        case Op::Drop: return "Drop";
        case Op::Add: return "Add";
        case Op::Sub: return "Sub";
        case Op::Mul: return "Mul";
        case Op::Div: return "Div";
        case Op::Mod: return "Mod";
        case Op::Equal: return "Equal";
        case Op::NotEqual: return "NotEqual";
        case Op::Less: return "Less";
        case Op::LessEqual: return "LessEqual";
        case Op::Greater: return "Greater";
        case Op::GreaterEqual: return "GreaterEqual";
        case Op::BranchZero: return "BranchZero";
        case Op::Jump: return "Jump";
        case Op::Call: return "Call";
        case Op::Return: return "Return";
        case Op::Trap: return "Trap";
    }
    throw std::runtime_error("MachineOpcode C++ không được khai báo trong V++");
}

bool symbolSafe(const std::string &name) {
    if (name.empty()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto ch = name[i];
        const bool alpha = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') || ch == '_';
        if (!alpha && !(i != 0 && ch >= '0' && ch <= '9')) return false;
    }
    return true;
}

std::int32_t checkedInt(std::size_t value, const char *label) {
    if (value > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::runtime_error(std::string("Machine IR V++ vượt i32: ") + label);
    return static_cast<std::int32_t>(value);
}

std::string targetOperatingSystem(const Target &target) {
    switch (target.os) {
        case OperatingSystem::Linux: return "linux";
        case OperatingSystem::MacOS: return "macos";
        case OperatingSystem::Windows: return "windows";
        default: throw std::runtime_error("V++ native encoder: hệ điều hành không hợp lệ");
    }
}

const char *targetAbi(const Target &target) {
    switch (target.abi) {
        case Abi::Gnu: return "gnu";
        case Abi::GnuEabiHf: return "gnueabihf";
        case Abi::Musl: return "musl";
        case Abi::MuslEabiHf: return "musleabihf";
        case Abi::Msvc: return "msvc";
        case Abi::Darwin: return "darwin";
        default: throw std::runtime_error("V++ object writer: ABI không hợp lệ");
    }
}

std::string makeVppObjectSource(const Target &target,
                                const std::vector<std::uint8_t> &bytes,
                                std::string_view symbol,
                                const std::vector<MachineRelocation> &relocations) {
    if (!symbolSafe(std::string(symbol)))
        throw std::runtime_error("tên symbol native không hợp lệ");
    if (bytes.empty()) throw std::runtime_error("không có mã máy để xuất object");
    const auto os = targetOperatingSystem(target);
    if ((os == "linux" && target.objectFormat != ObjectFormat::Elf) ||
        (os == "macos" && target.objectFormat != ObjectFormat::MachO) ||
        (os == "windows" && target.objectFormat != ObjectFormat::Coff))
        throw std::runtime_error("V++ object writer: định dạng object không khớp hệ điều hành");

    std::ostringstream source;
    source << "nhập gói.biên dịch.đối tượng.*;\n"
           << "hàm chính() {\n  code = [";
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) source << ',';
        source << static_cast<unsigned>(bytes[i]);
    }
    source << "];\n  relocs = [";
    for (std::size_t i = 0; i < relocations.size(); ++i) {
        const auto &reloc = relocations[i];
        if (!symbolSafe(reloc.symbol) || reloc.offset > bytes.size() ||
            bytes.size() - reloc.offset < 4)
            throw std::runtime_error("native object relocation offset/symbol không hợp lệ");
        if (i) source << ',';
        source << "{\"offset\":" << checkedInt(reloc.offset, "relocation offset")
               << ",\"symbol\":" << quoted(reloc.symbol) << '}';
    }
    source << "];\n  object = biên dịch đối tượng("
           << quoted(std::string(architectureName(target.arch))) << ','
           << quoted(os) << ',' << quoted(targetAbi(target)) << ",code,"
           << quoted(std::string(symbol)) << ",relocs);\n"
           << "  bảng = \"0123456789abcdef\";\n  hex = \"\";\n"
           << "  lặp (i = 0; i < độ dài(object); i++) {\n"
           << "    b = object[i];\n"
           << "    nếu (b < 0 || b > 255) { ném \"object: byte ngoài u8\"; };\n"
           << "    hex += bảng[(b - b % 16) / 16] + bảng[b % 16];\n"
           << "  };\n  in \"B:\" + hex;\n}\n";
    return source.str();
}

std::string makeSelfHostedObjectSource(const Target &target,
                                       std::string_view source,
                                       std::string_view functionName) {
    if (target.arch != Architecture::X86_64 && target.arch != Architecture::AArch64)
        throw std::runtime_error("native AST V++ chỉ hỗ trợ x86_64/AArch64");
    if (!symbolSafe(nativeAbiSymbol(functionName)))
        throw std::runtime_error("tên symbol native không hợp lệ");
    std::ostringstream code;
    code << "nhập gói.biên dịch.mã máy từ cây.*;\n"
         << "hàm chính() {\n"
         << "  kết_quả = tạo đối tượng hàm("
         << quoted(std::string(source)) << ','
         << quoted(std::string(functionName)) << ','
         << quoted(std::string(architectureName(target.arch))) << ','
         << quoted(targetOperatingSystem(target)) << ','
         << quoted(targetAbi(target)) << ");\n"
         << "  xuất bản ghi(kết_quả);\n"
         << "}\n";
    return code.str();
}

std::string makeSelfHostedProgramSource(const Target &target, std::string_view source) {
    if (target.arch != Architecture::X86_64 && target.arch != Architecture::AArch64)
        throw std::runtime_error("native AST V++ chỉ hỗ trợ x86_64/AArch64");
    std::ostringstream code;
    code << "nhập gói.biên dịch.mã máy từ cây.*;\n"
         << "hàm chính() {\n"
         << "  các_hàm = tạo đối tượng chương trình("
         << quoted(std::string(source)) << ','
         << quoted(std::string(architectureName(target.arch))) << ','
         << quoted(targetOperatingSystem(target)) << ','
         << quoted(targetAbi(target)) << ");\n"
         << "  lặp (i = 0; i < độ dài(các_hàm); i++) {\n"
         << "    in \"F:\" + các_hàm[i][\"name\"];\n"
         << "    xuất bản ghi(các_hàm[i]);\n"
         << "  };\n}\n";
    return code.str();
}

std::string makeLoweringSource(const compiler::IrProgram &ir,
                               std::string_view functionName, bool leaf = false,
                               const Target *emitTarget = nullptr,
                               bool emitObject = false, bool emitAssembly = false) {
    if (emitObject && !emitTarget)
        throw std::runtime_error("V++ object writer cần target phát mã máy");
    const auto abiName = nativeAbiSymbol(functionName);
    if (emitObject && !symbolSafe(abiName))
        throw std::runtime_error("tên symbol native không hợp lệ");
    if (emitTarget && !leaf && emitTarget->arch != Architecture::X86_64 &&
        emitTarget->arch != Architecture::AArch64)
        throw std::runtime_error("Machine IR V++ chỉ hỗ trợ x86_64/AArch64");
    std::ostringstream code;
    code << "nhập gói.biên dịch.mã máy hạ biểu diễn trung gian.*;\n";
    if (emitTarget) code << "nhập gói.biên dịch.mã máy.*;\n";
    if (emitObject) code << "nhập gói.biên dịch.đối tượng.*;\n";
    if (emitAssembly) code << "nhập gói.biên dịch.lắp ráp.*;\n";
    code << "hàm chính() {\n  ir = {\"values\":[";
    for (std::size_t i = 0; i < ir.values.size(); ++i) {
        const auto &v = ir.values[i];
        if (i) code << ',';
        code << "{\"opcode\":" << quoted(sourceValueOpcode(v.opcode))
             << ",\"symbolId\":" << v.symbolId
             << ",\"text\":" << quoted(v.text)
             << ",\"callTarget\":"
             << quoted(v.callTarget == compiler::CallTargetKind::DirectFunction
                           ? "DirectFunction" : "Invalid")
             << ",\"operands\":[";
        for (std::size_t j = 0; j < v.operands.size(); ++j) {
            if (j) code << ',';
            code << checkedInt(v.operands[j], "IR operand id");
        }
        code << "]}";
    }
    code << "],\"functions\":[";
    bool first = true;
    for (const auto &inst : ir.instructions) {
        if (inst.opcode != compiler::IrOpcode::DefineFunction) continue;
        if (!first) code << ',';
        first = false;
        writeSourceInstruction(code, inst);
    }
    code << "]};\n";
    if (leaf) {
        code << "  fn = biên dịch máy hạ lá(ir, " << quoted(abiName) << ");\n"
             << "  in \"L:\" + fn[\"kind\"] + \":\" + fn[\"constant\"];\n";
    } else {
        code << "  fn = biên dịch máy hạ IR(ir, " << quoted(abiName) << ");\n"
             << "  in \"M:\" + fn[\"parameterCount\"] + \":\" + fn[\"slotCount\"] + \":\" + fn[\"maxStackDepth\"];\n"
             << "  lệnh = fn[\"instructions\"];\n"
             << "  lặp (i = 0; i < độ dài(lệnh); i++) {\n"
             << "    op = lệnh[i];\n"
             << "    in \"I:\" + op[\"opcode\"] + \":\" + op[\"immediate\"] + \":\" + op[\"operand\"] + \":\" + op[\"depthBefore\"] + \":\" + op[\"symbol\"];\n"
             << "  };\n";
    }
    if (emitTarget) {
        const auto arch = quoted(architectureName(emitTarget->arch));
        const auto os = quoted(targetOperatingSystem(*emitTarget));
        if (leaf) {
            code << "  byte = biên dịch mã máy lá(" << arch
                 << ", fn[\"kind\"], fn[\"constant\"], " << os << ");\n";
        } else {
            code << "  kết_quả = biên dịch mã máy hàm(" << arch << ", fn, " << os << ");\n"
                 << "  byte = kết_quả[\"bytes\"];\n";
        }
        code << "  bảng = \"0123456789abcdef\";\n"
             << "  hex = \"\";\n"
             << "  lặp (i = 0; i < độ dài(byte); i++) {\n"
             << "    b = byte[i];\n"
             << "    nếu (b < 0 || b > 255) { ném \"V++ encoder trả byte ngoài u8\"; };\n"
             << "    hex += bảng[(b - b % 16) / 16] + bảng[b % 16];\n"
             << "  };\n"
             << "  in \"B:\" + hex;\n";
        if (!leaf) {
            code << "  tái_định_vị = kết_quả[\"relocations\"];\n"
                 << "  lặp (i = 0; i < độ dài(tái_định_vị); i++) {\n"
                 << "    mục = tái_định_vị[i];\n"
                 << "    in \"R:\" + mục[\"offset\"] + \":\" + mục[\"symbol\"];\n"
                 << "  };\n";
        }
        if (emitAssembly) {
            if (leaf) {
                code << "  asm = biên dịch lắp ráp lá(" << arch << ", " << os << ", "
                     << quoted(abiName) << ", fn[\"kind\"], fn[\"constant\"], byte);\n";
            } else {
                code << "  asm = biên dịch lắp ráp mã máy(" << arch << ", " << os << ", "
                     << quoted(abiName) << ", kết_quả);\n";
            }
            code << "  asm_bytes = chuỗi thành byte máy ảo(asm);\n"
                 << "  asm_hex = \"\";\n"
                 << "  lặp (i = 0; i < độ dài(asm_bytes); i++) {\n"
                 << "    b = asm_bytes[i];\n"
                 << "    asm_hex += bảng[(b - b % 16) / 16] + bảng[b % 16];\n"
                 << "  };\n"
                 << "  in \"A:\" + asm_hex;\n";
        }
        if (emitObject) {
            code << "  object = biên dịch đối tượng(" << arch << ", " << os << ", "
                 << quoted(targetAbi(*emitTarget)) << ", byte, "
                 << quoted(abiName) << ", "
                 << (leaf ? "[]" : "tái_định_vị") << ");\n"
                 << "  hex = \"\";\n"
                 << "  lặp (i = 0; i < độ dài(object); i++) {\n"
                 << "    b = object[i];\n"
                 << "    nếu (b < 0 || b > 255) { ném \"V++ object trả byte ngoài u8\"; };\n"
                 << "    hex += bảng[(b - b % 16) / 16] + bảng[b % 16];\n"
                 << "  };\n"
                 << "  in \"O:\" + hex;\n";
        }
    }
    code << "}\n";
    return code.str();
}

std::string makeVppSource(const Target &target, const MachineFunction &function) {
    std::string os;
    switch (target.os) {
        case OperatingSystem::Linux: os = "linux"; break;
        case OperatingSystem::MacOS: os = "macos"; break;
        case OperatingSystem::Windows: os = "windows"; break;
        default: throw std::runtime_error("Machine IR V++ chưa hỗ trợ hệ điều hành này");
    }
    if (target.arch != Architecture::X86_64 &&
        target.arch != Architecture::AArch64)
        throw std::runtime_error("Machine IR V++ chỉ hỗ trợ x86_64/AArch64");

    std::ostringstream code;
    code << "nhập gói.biên dịch.mã máy.*;\n"
         << "hàm chính() {\n  lệnh = [];\n";
    for (const auto &inst : function.instructions) {
        if (!inst.symbol.empty() && !symbolSafe(inst.symbol))
            throw std::runtime_error("Machine IR V++: ký hiệu CALL không hợp lệ");
        code << "  thêm(lệnh, biên dịch mã máy lệnh(\"" << opcodeName(inst.opcode)
             << "\", " << inst.immediate << ", " << checkedInt(inst.operand, "operand")
             << ", " << checkedInt(inst.depthBefore, "depthBefore")
             << ", \"" << inst.symbol << "\"));\n";
    }
    code << "  kết_quả = biên dịch mã máy hàm(\""
         << architectureName(target.arch) << "\", {\n"
         << "    \"parameterCount\": " << checkedInt(function.parameterCount, "parameterCount") << ",\n"
         << "    \"slotCount\": " << checkedInt(function.slotCount, "slotCount") << ",\n"
         << "    \"maxStackDepth\": " << checkedInt(function.maxStackDepth, "maxStackDepth") << ",\n"
         << "    \"instructions\": lệnh}, \"" << os << "\");\n"
         << "  byte = kết_quả[\"bytes\"];\n"
         << "  bảng = \"0123456789abcdef\";\n"
         << "  hex = \"\";\n"
         << "  lặp (i = 0; i < độ dài(byte); i++) {\n"
         << "    b = byte[i];\n"
         << "    nếu (b < 0 || b > 255) { ném \"V++ encoder trả byte ngoài u8\"; };\n"
         << "    hex += bảng[(b - b % 16) / 16] + bảng[b % 16];\n"
         << "  };\n"
         << "  in \"B:\" + hex;\n"
         << "  tái_định_vị = kết_quả[\"relocations\"];\n"
         << "  lặp (i = 0; i < độ dài(tái_định_vị); i++) {\n"
         << "    mục = tái_định_vị[i];\n"
         << "    in \"R:\" + mục[\"offset\"] + \":\" + mục[\"symbol\"];\n"
         << "  };\n}\n";
    return code.str();
}

std::string makeVppLeafSource(const Target &target, const RawLeaf &leaf) {
    std::string os;
    switch (target.os) {
        case OperatingSystem::Linux: os = "linux"; break;
        case OperatingSystem::MacOS: os = "macos"; break;
        case OperatingSystem::Windows: os = "windows"; break;
        default: throw std::runtime_error("V++ leaf encoder: hệ điều hành không hợp lệ");
    }
    const char *kind = nullptr;
    switch (leaf.kind) {
        case RawLeafKind::ReturnConstantI32: kind = "hằng"; break;
        case RawLeafKind::AddI32Arguments: kind = "cộng"; break;
        case RawLeafKind::SubI32Arguments: kind = "trừ"; break;
        default: throw std::runtime_error("V++ leaf encoder: loại hàm chưa định nghĩa");
    }
    std::ostringstream code;
    code << "nhập gói.biên dịch.mã máy.*;\n"
         << "hàm chính() {\n"
         << "  byte = biên dịch mã máy lá(\"" << architectureName(target.arch)
         << "\", \"" << kind << "\", " << leaf.constant << ", \"" << os << "\");\n"
         << "  bảng = \"0123456789abcdef\";\n"
         << "  hex = \"\";\n"
         << "  lặp (i = 0; i < độ dài(byte); i++) {\n"
         << "    b = byte[i];\n"
         << "    nếu (b < 0 || b > 255) { ném \"V++ leaf encoder trả byte ngoài u8\"; };\n"
         << "    hex += bảng[(b - b % 16) / 16] + bảng[b % 16];\n"
         << "  };\n"
         << "  in \"B:\" + hex;\n}\n";
    return code.str();
}

std::string makeVppTargetSource(std::string_view triple) {
    std::ostringstream code;
    code << "nhập gói.biên dịch.đích.*;\n"
         << "hàm chính() {\n"
         << "  đích = biên dịch phân tích đích(" << quoted(std::string(triple)) << ");\n"
         << "  in \"T:\" + đích[\"kiến trúc\"] + \":\" + đích[\"hệ điều hành\"] + \":\" + đích[\"ABI\"] + \":\" + đích[\"định dạng\"];\n"
         << "}\n";
    return code.str();
}

std::string executeVppSource(const std::string &source,
                             const std::filesystem::path &sourceBase,
                             std::string *partialOutput = nullptr) {
    const bool profile = std::getenv("VPP_NATIVE_STAGE0_PROFILE") != nullptr;
    const auto start = std::chrono::steady_clock::now();
    compiler::CompilationContext context;
    context.importResolutionBase = sourceBase.empty() ? std::filesystem::current_path() : sourceBase;
    context.currentSourceIdentity = "<native-machine-bridge>";
    const auto artifacts = compiler::compilePipeline(context, source, keywordMap, true);
    const auto afterFrontend = std::chrono::steady_clock::now();
    if (profile) {
        std::cerr << "native Stage 0 frontend: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(afterFrontend - start).count()
                  << " ms, source=" << source.size() << " bytes\n";
    }
    // Stage 0 lowers the wrapper to host assembly. Machine lowering, encoding,
    // and object writing still execute the V++ implementations in gói/biên dịch.
    // Stage 1+ never calls VM::run through this bridge.
    aot::Snapshot snapshot;
    snapshot.strings = context.stringPool;
    snapshot.entry = artifacts.bytecode;
    snapshot.foreignFunctions = context.foreignFunctions;
    for (const auto &[id, code] : context.functionBytecode) {
        const auto name = context.functionNameIndices.find(id);
        snapshot.functions.emplace(id, aot::FunctionCode{
            name == context.functionNameIndices.end() ? -1 : name->second, code});
    }
    for (const auto &module : context.moduleInitializers)
        snapshot.initializers.push_back(module.bytecode);

    std::filesystem::path root;
    for (const char *name : {"VPP_AOT_ROOT", "VPP_HOME"}) {
        if (const char *value = std::getenv(name); value && *value) {
            root = std::filesystem::absolute(value);
            break;
        }
    }
    if (root.empty()) {
        for (auto candidate = std::filesystem::absolute(context.importResolutionBase);;
             candidate = candidate.parent_path()) {
            if (std::filesystem::is_regular_file(candidate /
                    "scripts/quality/native_aot_link/CMakeLists.txt")) {
                root = candidate;
                break;
            }
            if (candidate == candidate.parent_path()) break;
        }
    }
    if (root.empty()) throw std::runtime_error("V++ native bridge cần VPP_HOME");

    struct TemporaryNativeOutput {
        std::filesystem::path directory;
        TemporaryNativeOutput() {
            static std::atomic<unsigned long> counter{0};
            for (int i = 0; i < 100; ++i) {
                directory = std::filesystem::temp_directory_path() /
                    ("vpp-machine-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(++counter));
                std::error_code error;
                if (std::filesystem::create_directory(directory, error)) return;
            }
            throw std::runtime_error("V++ native bridge: không tạo được thư mục tạm");
        }
        ~TemporaryNativeOutput() {
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
        }
    } temporary;
    const auto executable = temporary.directory /
#if defined(_WIN32)
        "machine-bridge.exe";
#else
        "machine-bridge";
#endif
    aot::compileExecutable(snapshot, executable, root);
    if (profile) {
        std::cerr << "native Stage 0 ASM/link: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - afterFrontend).count()
                  << " ms\n";
    }
    const auto result = aot::runProcess({executable.u8string()});
    if (profile) {
        std::cerr << "native Stage 0 bridge exit: " << result.status << '\n';
    }
    // Native wrappers emit newline-delimited text records. The Windows CRT
    // writes CRLF to stdout, while the target parser and machine/object
    // decoders consume LF records. Normalize at the bridge boundary so a
    // Windows target's final field is "COFF", not "COFF\r".
    std::string output;
    output.reserve(result.output.size());
    for (std::size_t i = 0; i < result.output.size(); ++i) {
        if (result.output[i] == '\r' && i + 1 < result.output.size() &&
            result.output[i + 1] == '\n') continue;
        output.push_back(result.output[i]);
    }
    if (partialOutput) *partialOutput += output;
    if (result.status != 0)
        throw std::runtime_error("V++ native bridge: executable thất bại (" +
            std::to_string(result.status) + ")");
    if (profile) {
        std::cerr << "native Stage 0 ASM/link/execute: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - afterFrontend).count()
                  << " ms\n";
    }
    return output;
}

std::size_t parseOffset(const std::string &s) {
    std::size_t value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size())
        throw std::runtime_error("V++ encoder: relocation offset không hợp lệ");
    return value;
}

std::int32_t parseI32(const std::string &s) {
    std::int32_t result = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
    if (ec != std::errc{} || ptr != s.data() + s.size())
        throw std::runtime_error("V++ lowering: immediate không hợp lệ");
    return result;
}

MachineOpcode parseMachineOpcode(const std::string &name) {
    for (int i = static_cast<int>(MachineOpcode::PushConstant);
         i <= static_cast<int>(MachineOpcode::Trap); ++i) {
        auto op = static_cast<MachineOpcode>(i);
        if (name == opcodeName(op)) return op;
    }
    throw std::runtime_error("V++ lowering trả MachineOpcode không được định nghĩa: " + name);
}

MachineFunction decodeLowering(const std::string &text) {
    MachineFunction result;
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 2, "M:") != 0)
        throw std::runtime_error("V++ lowering thiếu MachineFunction header M:");
    const auto first = line.find(':', 2);
    const auto second = first == std::string::npos ? first : line.find(':', first + 1);
    if (second == std::string::npos)
        throw std::runtime_error("V++ lowering: header không hợp lệ");
    result.parameterCount = parseOffset(line.substr(2, first - 2));
    result.slotCount = parseOffset(line.substr(first + 1, second - first - 1));
    result.maxStackDepth = parseOffset(line.substr(second + 1));
    const auto maxFrame = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8);
    if (result.parameterCount > result.slotCount ||
        result.slotCount > maxFrame || result.maxStackDepth > maxFrame ||
        result.slotCount + result.maxStackDepth > maxFrame)
        throw std::runtime_error("V++ lowering trả frame sai ABI");
    while (std::getline(lines, line)) {
        if (line.compare(0, 2, "I:") != 0)
            throw std::runtime_error("V++ lowering: bản ghi lệnh không hợp lệ");
        std::size_t pos = 2;
        std::string parts[5];
        for (int i = 0; i < 4; ++i) {
            const auto sep = line.find(':', pos);
            if (sep == std::string::npos)
                throw std::runtime_error("V++ lowering: thiếu trường opcode");
            parts[i] = line.substr(pos, sep - pos);
            pos = sep + 1;
        }
        parts[4] = line.substr(pos);
        if (!parts[4].empty() && !symbolSafe(parts[4]))
            throw std::runtime_error("V++ lowering: CALL symbol không hợp lệ");
        result.instructions.push_back({parseMachineOpcode(parts[0]), parseI32(parts[1]),
                                       parseOffset(parts[2]), parseOffset(parts[3]), parts[4]});
        if (result.instructions.size() > 100000)
            throw std::runtime_error("V++ lowering: quá nhiều Machine IR instructions");
    }
    if (result.instructions.empty())
        throw std::runtime_error("V++ lowering không trả Machine IR instructions");
    return result;
}

RawLeaf decodeLeafLowering(const std::string &text) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 2, "L:") != 0)
        throw std::runtime_error("thiếu bản ghi leaf L:");
    const auto delim = line.find(':', 2);
    if (delim == std::string::npos)
        throw std::runtime_error("bản ghi leaf thiếu constant");
    RawLeaf result;
    const auto kind = line.substr(2, delim - 2);
    if (kind == "hằng") result.kind = RawLeafKind::ReturnConstantI32;
    else if (kind == "cộng") result.kind = RawLeafKind::AddI32Arguments;
    else if (kind == "trừ") result.kind = RawLeafKind::SubI32Arguments;
    else throw std::runtime_error("leaf kind V++ không hợp lệ: " + kind);
    result.constant = parseI32(line.substr(delim + 1));
    if (std::getline(lines, line))
        throw std::runtime_error("bản ghi leaf thừa");
    return result;
}

unsigned hexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a') + 10;
    throw std::runtime_error("V++ encoder trả hex không hợp lệ");
}

RawLeafResult decodeLeafOutput(const std::string &text) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 2, "B:") != 0 ||
        line.size() <= 2 || (line.size() - 2) % 2 != 0)
        throw std::runtime_error("thiếu bản ghi byte B:");
    RawLeafResult result;
    for (std::size_t i = 2; i < line.size(); i += 2)
        result.bytes.push_back(static_cast<std::uint8_t>(hexNibble(line[i]) * 16 +
                                                         hexNibble(line[i + 1])));
    if (std::getline(lines, line))
        throw std::runtime_error("có bản ghi thừa trong phản hồi encoder");
    return result;
}

std::size_t encodedRecordStart(const std::string &output) {
    // Lowering always emits complete newline-delimited L:/M:+I: records first.
    const auto pos = output.find("\nB:");
    if (pos == std::string::npos)
        throw std::runtime_error("V++ không trả bản ghi byte sau khi hạ IR");
    return pos + 1;
}

// The production object path emits machine bytes and relocations before the
// object. Keep both records to validate the ISA emitter and the object writer.
std::size_t encodedObjectRecordStart(const std::string &output) {
    const auto pos = output.find("\nO:");
    if (pos == std::string::npos)
        throw std::runtime_error("V++ không trả bản ghi object O:");
    return pos + 1;
}

RawObjectResult decodeObjectOutput(const std::string &text) {
    if (text.compare(0, 2, "O:") != 0)
        throw std::runtime_error("V++ object writer thiếu bản ghi O:");
    auto decoded = decodeLeafOutput("B:" + text.substr(2));
    return {std::move(decoded.bytes), {}};
}

MachineEmission decodeOutput(const std::string &text, const MachineFunction &function) {
    MachineEmission result;
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 2, "B:") != 0 || line.size() <= 2 ||
        ((line.size() - 2) % 2) != 0)
        throw std::runtime_error("V++ encoder thiếu bản ghi byte B:");
    for (std::size_t i = 2; i < line.size(); i += 2)
        result.bytes.push_back(static_cast<std::uint8_t>(hexNibble(line[i]) * 16 + hexNibble(line[i + 1])));
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        if (line.compare(0, 2, "A:") == 0) {
            if (!result.assembly.empty() || line.size() <= 2 || (line.size() - 2) % 2)
                throw std::runtime_error("V++ assembly trả bản ghi A: không hợp lệ");
            for (std::size_t i = 2; i < line.size(); i += 2)
                result.assembly.push_back(static_cast<char>(hexNibble(line[i]) * 16 +
                                                             hexNibble(line[i + 1])));
            if (result.assembly.compare(0, 6, ".text\n") != 0)
                throw std::runtime_error("V++ assembly thiếu section .text");
            continue;
        }
        if (line.compare(0, 2, "R:") != 0)
            throw std::runtime_error("V++ encoder trả bản ghi không hợp lệ");
        const auto colon = line.find(':', 2);
        if (colon == std::string::npos)
            throw std::runtime_error("V++ encoder: relocation thiếu symbol");
        const auto offset = parseOffset(line.substr(2, colon - 2));
        const auto symbol = line.substr(colon + 1);
        if (!symbolSafe(symbol) || offset > result.bytes.size() || result.bytes.size() - offset < 4)
            throw std::runtime_error("V++ encoder: relocation không hợp lệ");
        result.relocations.push_back({offset, symbol});
    }
    std::size_t callCount = 0;
    for (const auto &inst : function.instructions)
        if (inst.opcode == MachineOpcode::Call) {
            if (callCount >= result.relocations.size() ||
                result.relocations[callCount].symbol != inst.symbol)
                throw std::runtime_error("V++ encoder: relocation CALL thiếu hoặc sai thứ tự");
            ++callCount;
        }
    if (callCount != result.relocations.size())
        throw std::runtime_error("V++ encoder: relocation thừa");
    return result;
}

} // namespace

TargetParseResult parseTargetVpp(std::string_view triple,
                                 const std::filesystem::path &sourceBase) {
    try {
        const auto output = executeVppSource(makeVppTargetSource(triple), sourceBase);
        if (output.compare(0, 2, "T:") != 0 || output.find('\n') != output.size() - 1)
            throw std::runtime_error("bản ghi target V++ không hợp lệ");
        std::string fields[4];
        std::size_t start = 2;
        for (int i = 0; i < 3; ++i) {
            const auto sep = output.find(':', start);
            if (sep == std::string::npos)
                throw std::runtime_error("target V++ thiếu trường");
            fields[i] = output.substr(start, sep - start);
            start = sep + 1;
        }
        fields[3] = output.substr(start, output.size() - start - 1);
        Target target;
        for (const auto &info : registeredArchitectures())
            if (fields[0] == info.name) target.arch = info.arch;
        if (target.arch == Architecture::Unknown)
            throw std::runtime_error("target V++ trả kiến trúc chưa đăng ký");
        if (fields[1] == "linux") target.os = OperatingSystem::Linux;
        else if (fields[1] == "macos") target.os = OperatingSystem::MacOS;
        else if (fields[1] == "windows") target.os = OperatingSystem::Windows;
        else throw std::runtime_error("target V++ trả hệ điều hành không hợp lệ");
        if (fields[2] == "gnu") target.abi = Abi::Gnu;
        else if (fields[2] == "gnueabihf") target.abi = Abi::GnuEabiHf;
        else if (fields[2] == "musl") target.abi = Abi::Musl;
        else if (fields[2] == "musleabihf") target.abi = Abi::MuslEabiHf;
        else if (fields[2] == "msvc") target.abi = Abi::Msvc;
        else if (fields[2] == "darwin") target.abi = Abi::Darwin;
        else throw std::runtime_error("target V++ trả ABI không hợp lệ");
        if (fields[3] == "ELF") target.objectFormat = ObjectFormat::Elf;
        else if (fields[3] == "COFF") target.objectFormat = ObjectFormat::Coff;
        else if (fields[3] == "Mach-O") target.objectFormat = ObjectFormat::MachO;
        else throw std::runtime_error("target V++ trả định dạng object không hợp lệ");
        return {target, {}};
    } catch (const std::exception &e) {
        return {{}, std::string("V++ target parser: ") + e.what()};
    }
}

MachineFunctionLowering lowerMachineFunctionVpp(const compiler::IrProgram &ir,
                                                std::string_view functionName,
                                                const std::filesystem::path &sourceBase) {
    try {
        return {decodeLowering(executeVppSource(makeLoweringSource(ir, functionName),
                                                sourceBase)), {}};
    } catch (const std::exception &e) {
        return {{}, std::string("V++ Machine IR lowering: ") + e.what()};
    }
}

RawLeafLowering lowerRawLeafVpp(const compiler::IrProgram &ir,
                                std::string_view functionName,
                                const std::filesystem::path &sourceBase) {
    try {
        return {decodeLeafLowering(executeVppSource(
            makeLoweringSource(ir, functionName, true), sourceBase)), {}};
    } catch (const std::exception &e) {
        return {{}, std::string("V++ native leaf lowering: ") + e.what()};
    }
}

MachineEmission emitMachineFunctionVpp(const Target &target,
                                      const MachineFunction &function,
                                      const std::filesystem::path &sourceBase) {
    try {
        // V++ itself validates the Machine IR and throws for unsupported opcodes.
        return decodeOutput(executeVppSource(makeVppSource(target, function), sourceBase), function);
    } catch (const std::exception &e) {
        return {{}, {}, std::string("V++ native encoder: ") + e.what(), {}};
    }
}

RawLeafResult emitRawLeafVpp(const Target &target, const RawLeaf &leaf,
                             const std::filesystem::path &sourceBase) {
    try {
        return decodeLeafOutput(executeVppSource(makeVppLeafSource(target, leaf), sourceBase));
    } catch (const std::exception &e) {
        return {{}, std::string("V++ native leaf encoder: ") + e.what()};
    }
}

RawObjectResult emitRawObjectBytesVpp(const Target &target,
                                      const std::vector<std::uint8_t> &code,
                                      std::string_view symbol,
                                      const std::vector<MachineRelocation> &relocations,
                                      const std::filesystem::path &sourceBase) {
    try {
        const auto output = executeVppSource(
            makeVppObjectSource(target, code, symbol, relocations), sourceBase);
        auto decoded = decodeLeafOutput(output);
        return {std::move(decoded.bytes), {}};
    } catch (const std::exception &e) {
        return {{}, std::string("V++ object writer: ") + e.what()};
    }
}

VppCompiledLeaf compileRawLeafVpp(const Target &target, const compiler::IrProgram &ir,
                                  std::string_view functionName,
                                  const std::filesystem::path &sourceBase,
                                  bool emitObject, bool emitAssembly) {
    VppCompiledLeaf result;
    std::string partial;
    try {
        const auto output = executeVppSource(
            makeLoweringSource(ir, functionName, true, &target, emitObject, emitAssembly), sourceBase, &partial);
        const auto start = encodedRecordStart(output);
        result.leaf = decodeLeafLowering(output.substr(0, start));
        result.lowered = true;
        if (emitObject) {
            const auto objectStart = encodedObjectRecordStart(output);
            result.emission = decodeLeafOutput(output.substr(start, objectStart - start));
            result.object = decodeObjectOutput(output.substr(objectStart));
        } else if (emitAssembly) {
            const auto decoded = decodeOutput(output.substr(start), {});
            result.emission = {decoded.bytes, {}};
            result.assembly = decoded.assembly;
            if (result.assembly.empty())
                throw std::runtime_error("V++ leaf assembly không trả bản ghi A:");
        } else {
            result.emission = decodeLeafOutput(output.substr(start));
        }
    } catch (const std::exception &e) {
        // An L: record proves the leaf lowerer succeeded. Never interpret an
        // encoder error after this marker as an invitation to use another ISA.
        result.lowered = result.lowered || partial.compare(0, 2, "L:") == 0;
        result.error = std::string("V++ native leaf compile: ") + e.what();
    }
    return result;
}

VppCompiledMachine compileMachineFunctionVpp(const Target &target,
                                             const compiler::IrProgram &ir,
                                             std::string_view functionName,
                                             const std::filesystem::path &sourceBase,
                                             bool emitObject, bool emitAssembly) {
    VppCompiledMachine result;
    try {
        const auto output = executeVppSource(
            makeLoweringSource(ir, functionName, false, &target, emitObject, emitAssembly), sourceBase);
        const auto start = encodedRecordStart(output);
        result.function = decodeLowering(output.substr(0, start));
        if (emitObject) {
            const auto objectStart = encodedObjectRecordStart(output);
            result.emission = decodeOutput(output.substr(start, objectStart - start), result.function);
            result.object = decodeObjectOutput(output.substr(objectStart));
        } else {
            result.emission = decodeOutput(output.substr(start), result.function);
        }
    } catch (const std::exception &e) {
        result.error = std::string("V++ native Machine IR compile: ") + e.what();
    }
    return result;
}

VppCompiledMachine compileSourceMachineFunctionVpp(const Target &target,
                                                   std::string_view source,
                                                   std::string_view functionName,
                                                   const std::filesystem::path &sourceBase) {
    VppCompiledMachine result;
    try {
        // This generated V++ wrapper imports the compiler library; its import
        // root is independent of the directory containing the input .vi file.
        // Prefer the active workspace when it contains the bundled compiler;
        // otherwise preserve the source-base / VPP_HOME resolution paths.
        const auto workspace = std::filesystem::current_path();
        const auto bundledCompiler = std::filesystem::u8path(
            "gói/biên dịch/mã máy từ cây.vi");
        const auto wrapperBase = std::filesystem::exists(workspace / bundledCompiler)
            ? workspace : sourceBase;
        const auto output = executeVppSource(
            makeSelfHostedObjectSource(target, source, functionName), wrapperBase);
        const auto start = encodedRecordStart(output);
        const auto objectStart = encodedObjectRecordStart(output);
        result.function = decodeLowering(output.substr(0, start));
        result.emission = decodeOutput(output.substr(start, objectStart - start), result.function);
        result.object = decodeObjectOutput(output.substr(objectStart));
    } catch (const std::exception &e) {
        result.error = std::string("V++ self-host native compile: ") + e.what();
    }
    return result;
}

VppCompiledProgram compileSourceProgramVpp(const Target &target,
                                          std::string_view source,
                                          const std::filesystem::path &sourceBase) {
    VppCompiledProgram program;
    try {
        const auto workspace = std::filesystem::current_path();
        const auto bundledCompiler = std::filesystem::u8path("gói/biên dịch/mã máy từ cây.vi");
        const auto wrapperBase = std::filesystem::exists(workspace / bundledCompiler)
            ? workspace : sourceBase;
        const auto output = executeVppSource(makeSelfHostedProgramSource(target, source), wrapperBase);
        std::istringstream stream(output);
        std::ostringstream records;
        std::string currentName;
        std::string line;
        std::unordered_set<std::string> names;
        const auto finishFunction = [&]() {
            if (currentName.empty()) return;
            const auto data = records.str();
            const auto byteStart = encodedRecordStart(data);
            const auto objectStart = encodedObjectRecordStart(data);
            VppCompiledMachine compiled;
            compiled.function = decodeLowering(data.substr(0, byteStart));
            compiled.emission = decodeOutput(data.substr(byteStart, objectStart - byteStart),
                                             compiled.function);
            compiled.object = decodeObjectOutput(data.substr(objectStart));
            if (!compiled || !compiled.object)
                throw std::runtime_error("object native không hợp lệ: " + currentName);
            program.functions.emplace_back(currentName, std::move(compiled));
            records.str("");
            records.clear();
        };
        while (std::getline(stream, line)) {
            if (line.compare(0, 2, "F:") == 0) {
                finishFunction();
                currentName = line.substr(2);
                if (!symbolSafe(currentName) || !names.insert(currentName).second)
                    throw std::runtime_error("tên hàm native trùng hoặc không hợp lệ: " + currentName);
                if (names.size() > 4096)
                    throw std::runtime_error("native AOT vượt giới hạn 4096 hàm");
            } else if (!currentName.empty()) {
                records << line << '\n';
            } else {
                throw std::runtime_error("native AOT thiếu tên hàm F:");
            }
        }
        finishFunction();
        if (program.functions.empty())
            throw std::runtime_error("native AOT cần ít nhất một hàm");
    } catch (const std::exception &e) {
        program.functions.clear();
        program.error = std::string("V++ self-host native program: ") + e.what();
    }
    return program;
}

} // namespace vietvm::native
