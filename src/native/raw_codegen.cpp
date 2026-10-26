#include "vpp/native/raw_codegen.h"
#include "vpp/native/abi_symbol.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

#include "raw_backend.h"

namespace vietvm::native {
namespace {

using Backend = BackendEmission (*)(const RawLeaf &, const Target &);

Backend backendFor(Architecture arch) {
    switch (arch) {
        case Architecture::X86_64: return emitX86_64;
        case Architecture::AArch64: return emitAArch64;
        case Architecture::RiscV64: return emitRiscV64;
        case Architecture::ArmV7: return emitArmV7;
        case Architecture::I386: return emitI386;
        case Architecture::PowerPc: return emitPowerPc;
        case Architecture::PowerPc64: return emitPowerPc64;
        case Architecture::PowerPc64Le: return emitPowerPc64Le;
        case Architecture::Mips: return emitMips;
        case Architecture::MipsLe: return emitMipsLe;
        case Architecture::Mips64: return emitMips64;
        case Architecture::Mips64Le: return emitMips64Le;
        case Architecture::LoongArch64: return emitLoongArch64;
        case Architecture::S390X: return emitS390X;
        case Architecture::Sparc: return emitSparc;
        case Architecture::Sparc64: return emitSparc64;
        default: return nullptr;
    }
}

bool parseInteger(std::string_view text, std::int64_t &out) {
    if (text.empty()) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

bool fitsI32(std::int64_t value) {
    return value >= std::numeric_limits<std::int32_t>::min() &&
           value <= std::numeric_limits<std::int32_t>::max();
}

// Fold only pure arithmetic. Check *each* intermediate result so the raw AOT
// path never silently adopts CPU wraparound or a different overflow contract.
// The depth limit also rejects malformed cyclic IR instead of recursing forever.
bool constant(const compiler::IrProgram &ir, compiler::IrValueId id,
              std::int32_t &out, std::size_t depth = 0) {
    if (depth >= 64) return false;
    const auto *value = ir.value(id);
    if (!value) return false;

    if (value->opcode == compiler::IrValueOpcode::ConstInt) {
        std::int64_t parsed = 0;
        if (!value->operands.empty() || !parseInteger(value->text, parsed) ||
            !fitsI32(parsed)) return false;
        out = static_cast<std::int32_t>(parsed);
        return true;
    }
    if (value->opcode == compiler::IrValueOpcode::Unary &&
        value->text == "+" && value->operands.size() == 1)
        return constant(ir, value->operands[0], out, depth + 1);
    if (value->opcode == compiler::IrValueOpcode::Unary &&
        value->text == "-" && value->operands.size() == 1) {
        const auto *nested = ir.value(value->operands[0]);
        std::int64_t literal = 0;
        // The token 2147483648 only represents i32 when directly negated.
        if (nested && nested->opcode == compiler::IrValueOpcode::ConstInt &&
            nested->operands.empty() && parseInteger(nested->text, literal) &&
            literal == 2147483648LL) {
            out = std::numeric_limits<std::int32_t>::min();
            return true;
        }
        std::int32_t operand = 0;
        if (!constant(ir, value->operands[0], operand, depth + 1) ||
            operand == std::numeric_limits<std::int32_t>::min()) return false;
        out = -operand;
        return true;
    }
    if (value->opcode == compiler::IrValueOpcode::Binary &&
        value->operands.size() == 2 &&
        (value->text == "+" || value->text == "-" || value->text == "*")) {
        std::int32_t left = 0, right = 0;
        if (!constant(ir, value->operands[0], left, depth + 1) ||
            !constant(ir, value->operands[1], right, depth + 1)) return false;
        std::int64_t result = 0;
        if (value->text == "+") result = std::int64_t(left) + right;
        else if (value->text == "-") result = std::int64_t(left) - right;
        else result = std::int64_t(left) * right;
        if (!fitsI32(result)) return false;
        out = static_cast<std::int32_t>(result);
        return true;
    }
    return false;
}

bool symbolNameSafe(std::string_view name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
        return false;
    for (unsigned char ch : name)
        if (!(std::isalnum(ch) || ch == '_')) return false;
    return true;
}

// Assembly entry labels originate from source identifiers. Preserve
// Vietnamese identifiers (UTF-8 and spaces), but reject punctuation that
// could be interpreted as assembler directives before ABI mangling.
bool sourceSymbolSafe(std::string_view name) {
    if (name.empty()) return false;
    for (unsigned char ch : name) {
        if (ch >= 128) continue; // UTF-8 validity is checked by nativeAbiSymbol.
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == ' ') continue;
        return false;
    }
    return true;
}
} // namespace

RawLeafLowering lowerRawLeaf(const compiler::IrProgram &ir,
                             std::string_view functionName) {
    const compiler::IrInstruction *function = nullptr;
    for (const auto &inst : ir.instructions) {
        if (inst.opcode == compiler::IrOpcode::DefineFunction &&
            inst.declarationName == functionName) {
            if (function) return {{}, "có nhiều hàm cùng tên"};
            function = &inst;
        }
    }
    if (!function) return {{}, "không tìm thấy hàm: " + std::string(functionName)};
    if (function->unsupportedDirectRegion || !function->expressionRoots.empty() ||
        function->children.size() != 1 ||
        function->children[0].opcode != compiler::IrOpcode::Block)
        return {{}, "cấu trúc hàm chưa được hỗ trợ"};

    const auto &body = function->children[0];
    if (body.unsupportedDirectRegion || !body.expressionRoots.empty() ||
        body.children.size() != 1 ||
        body.children[0].opcode != compiler::IrOpcode::Return)
        return {{}, "hàm lá cần duy nhất một lệnh trả về"};
    const auto &ret = body.children[0];
    if (ret.unsupportedDirectRegion || !ret.children.empty() ||
        ret.expressionRoots.size() != 1)
        return {{}, "giá trị trả về chưa được hỗ trợ"};
    if (function->parameters.empty()) {
        std::int32_t value;
        if (constant(ir, ret.expressionRoots[0], value))
            return {{RawLeafKind::ReturnConstantI32, value}, {}};
    }
    if (function->parameters.size() == 2) {
        const auto &params = function->parameters;
        if (params[0].hasDefault || params[1].hasDefault ||
            params[0].symbolId < 0 || params[1].symbolId < 0 ||
            params[0].symbolId == params[1].symbolId)
            return {{}, "ABI i32 không hỗ trợ tham số mặc định hoặc trùng tên"};
        const auto *root = ir.value(ret.expressionRoots[0]);
        if (root && root->opcode == compiler::IrValueOpcode::Binary &&
            (root->text == "+" || root->text == "-") &&
            root->operands.size() == 2) {
            const auto *left = ir.value(root->operands[0]);
            const auto *right = ir.value(root->operands[1]);
            if (left && right &&
                left->opcode == compiler::IrValueOpcode::LoadName &&
                right->opcode == compiler::IrValueOpcode::LoadName &&
                left->operands.empty() && right->operands.empty()) {
                if (left->symbolId == params[0].symbolId &&
                    right->symbolId == params[1].symbolId)
                    return {{root->text == "+" ? RawLeafKind::AddI32Arguments
                                                : RawLeafKind::SubI32Arguments, 0}, {}};
                if (root->text == "+" &&
                    left->symbolId == params[1].symbolId &&
                    right->symbolId == params[0].symbolId)
                    return {{RawLeafKind::AddI32Arguments, 0}, {}};
            }
        }
    }
    return {{}, "chỉ hỗ trợ biểu thức hằng i32 (+,-,*), a + b, b + a hoặc a - b với hai tham số i32"};
}

RawLeafResult emitRawLeaf(const Target &target, const RawLeaf &leaf) {
    // Target recognition is independent of code generation support. Windows
    // ARM64 uses a different platform ABI; do not emit Darwin/Linux leaf
    // bodies under a valid but unsupported Windows target.
    if (target.arch == Architecture::AArch64 &&
        target.os == OperatingSystem::Windows)
        return {{}, "Windows ARM64 native ABI chưa được triển khai"};
    const auto backend = backendFor(target.arch);
    if (!backend) return {{}, "kiến trúc không có encoder"};
    const auto output = backend(leaf, target);
    return {output.bytes, output.error};
}

RawAssemblyResult emitRawAssembly(const Target &target, const RawLeaf &leaf,
                                  std::string_view symbol) {
    if (!sourceSymbolSafe(symbol)) return {{}, "tên nhãn assembly không hợp lệ"};
    const auto abiName = nativeAbiSymbol(symbol);
    if (!symbolNameSafe(abiName)) return {{}, "tên nhãn assembly không hợp lệ"};
    if (target.arch == Architecture::AArch64 &&
        target.os == OperatingSystem::Windows)
        return {{}, "Windows ARM64 native ABI chưa được triển khai"};
    const auto backend = backendFor(target.arch);
    if (!backend) return {{}, "kiến trúc không có encoder"};
    const auto output = backend(leaf, target);
    if (!output.error.empty()) return {{}, output.error};
    if (output.bytes.empty() || output.assembly.empty())
        return {{}, "backend chưa tạo đủ mã máy và assembly"};
    const std::string label =
        (target.os == OperatingSystem::MacOS ? "_" : "") + abiName;
    // x86 emitters write Intel mnemonics. GNU/LLVM integrated assemblers
    // default to AT&T syntax, which rejects `mov eax, edi` without this.
    const std::string dialect =
        (target.arch == Architecture::X86_64 || target.arch == Architecture::I386)
        ? ".intel_syntax noprefix\n" : "";
    return {dialect + ".text\n.globl " + label + "\n" + label + ":\n" + output.assembly, {}};
}

namespace {

bool pureConstantArithmetic(const compiler::IrProgram &ir,
                            compiler::IrValueId id, std::size_t nesting = 0) {
    if (nesting >= 64) return false;
    const auto *value = ir.value(id);
    if (!value) return false;
    using V = compiler::IrValueOpcode;
    if (value->opcode == V::ConstInt) return value->operands.empty();
    if (value->opcode == V::Unary && value->operands.size() == 1 &&
        (value->text == "+" || value->text == "-"))
        return pureConstantArithmetic(ir, value->operands[0], nesting + 1);
    if (value->opcode == V::Binary && value->operands.size() == 2 &&
        (value->text == "+" || value->text == "-" || value->text == "*"))
        return pureConstantArithmetic(ir, value->operands[0], nesting + 1) &&
               pureConstantArithmetic(ir, value->operands[1], nesting + 1);
    return false;
}

// Lower structured IR into a typed i32 stack machine. No VM heap values,
// implicit casts, external calls or exception edges are accepted here.
// Evaluation depth is tracked at every statement boundary; each branch and
// loop starts/ends at the same depth and can safely share a frame.
class MachineLowerer {
public:
    explicit MachineLowerer(const compiler::IrProgram &source) : ir(source) {}

    MachineFunctionLowering run(const compiler::IrInstruction &function) {
        if (function.unsupportedDirectRegion || !function.expressionRoots.empty() ||
            function.children.size() != 1 ||
            function.children.front().opcode != compiler::IrOpcode::Block)
            return {{}, "hàm native phải có thân IR trực tiếp"};
        result.parameterCount = function.parameters.size();
        for (const auto &parameter : function.parameters) {
            if (parameter.hasDefault || !bind(parameter.symbolId))
                return {{}, "tham số native cần symbol ID riêng và không có mặc định"};
        }
        if (!declareLocals(function.children.front())) return {{}, error};
        result.slotCount = slots.size();
        if (!statement(function.children.front()) || !hasReturn) return {{},
            error.empty() ? "hàm native cần trả về i32" : error};
        // An implicit fallthrough must fault. We never manufacture a return
        // value for a missing source-level return statement.
        add(MachineOpcode::Trap);
        return {std::move(result), {}};
    }

private:
    const compiler::IrProgram &ir;
    MachineFunction result;
    std::unordered_map<int, std::size_t> slots;
    std::string error;
    std::size_t depth = 0;
    bool hasReturn = false;
    struct LoopFixups { std::vector<std::size_t> breaks, continues; };
    std::vector<LoopFixups> loops;

    bool fail(std::string why) { error = std::move(why); return false; }
    bool bind(int symbol) {
        if (symbol < 0 || slots.count(symbol)) return false;
        slots.emplace(symbol, slots.size());
        return true;
    }
    std::size_t add(MachineOpcode opcode, std::int32_t immediate = 0,
                    std::size_t operand = 0) {
        result.instructions.push_back({opcode, immediate, operand, depth, {}});
        return result.instructions.size() - 1;
    }
    void push() {
        ++depth;
        if (depth > result.maxStackDepth) result.maxStackDepth = depth;
    }
    void drop() { add(MachineOpcode::Drop); --depth; }

    bool declareLocals(const compiler::IrInstruction &inst) {
        if (inst.opcode == compiler::IrOpcode::DeclareVariable && !bind(inst.symbolId))
            return fail("khai báo native thiếu symbol ID hoặc trùng symbol");
        if (inst.opcode == compiler::IrOpcode::Loop && !inst.expressionRoots.empty()) {
            const auto *init = ir.value(inst.expressionRoots.front());
            const auto *lhs = init && !init->operands.empty()
                ? ir.value(init->operands.front()) : nullptr;
            if (lhs && lhs->opcode == compiler::IrValueOpcode::LoadName &&
                lhs->symbolId >= 0 && !slots.count(lhs->symbolId)) {
                if (!bind(lhs->symbolId)) return fail("biến đếm vòng lặp không hợp lệ");
            }
        }
        for (const auto &child : inst.children)
            if (!declareLocals(child)) return false;
        return true;
    }

    bool value(compiler::IrValueId id, std::size_t nesting = 0) {
        if (nesting >= 64) return fail("biểu thức native quá sâu hoặc có chu trình");
        const auto *v = ir.value(id);
        if (!v) return fail("IR chứa giá trị không tồn tại");
        std::int32_t folded = 0;
        if (constant(ir, id, folded)) {
            add(MachineOpcode::PushConstant, folded); push(); return true;
        }
        if (pureConstantArithmetic(ir, id))
            return fail("biểu thức hằng native vượt phạm vi i32 hoặc tràn trung gian");
        using V = compiler::IrValueOpcode;
        switch (v->opcode) {
            case V::ConstBool:
                if (v->text != "đúng" && v->text != "sai" &&
                    v->text != "true" && v->text != "false")
                    return fail("hằng bool native không hợp lệ");
                add(MachineOpcode::PushConstant,
                    v->text == "đúng" || v->text == "true" ? 1 : 0);
                push(); return true;
            case V::LoadName: {
                const auto found = slots.find(v->symbolId);
                if (found == slots.end()) return fail("biến native chưa có slot hoặc binding");
                add(MachineOpcode::LoadSlot, 0, found->second); push(); return true;
            }
            case V::StoreName: {
                if (v->text != "=" || v->operands.size() != 2)
                    return fail("native chưa hỗ trợ phép gán mở rộng");
                const auto *lhs = ir.value(v->operands[0]);
                if (!lhs || lhs->opcode != V::LoadName)
                    return fail("đích gán native không phải biến");
                const auto found = slots.find(lhs->symbolId);
                if (found == slots.end()) return fail("biến được gán chưa có slot");
                if (!value(v->operands[1], nesting + 1)) return false;
                add(MachineOpcode::StoreSlot, 0, found->second);
                return true; // Assignment evaluates to the assigned value.
            }
            case V::Unary:
                if (v->operands.size() != 1) return fail("unary IR sai số toán hạng");
                if (v->text == "+") return value(v->operands[0], nesting + 1);
                if (v->text == "-") {
                    add(MachineOpcode::PushConstant, 0); push();
                    if (!value(v->operands[0], nesting + 1)) return false;
                    add(MachineOpcode::Sub); --depth; return true;
                }
                if (v->text == "!") {
                    if (!value(v->operands[0], nesting + 1)) return false;
                    add(MachineOpcode::PushConstant, 0); push();
                    add(MachineOpcode::Equal); --depth; return true;
                }
                return fail("toán tử unary native chưa hỗ trợ: " + v->text);
            case V::Binary: {
                if (v->operands.size() != 2)
                    return fail("binary IR sai số toán hạng");
                MachineOpcode op;
                if (v->text == "+") op = MachineOpcode::Add;
                else if (v->text == "-") op = MachineOpcode::Sub;
                else if (v->text == "*") op = MachineOpcode::Mul;
                else if (v->text == "==") op = MachineOpcode::Equal;
                else if (v->text == "!=") op = MachineOpcode::NotEqual;
                else if (v->text == "<") op = MachineOpcode::Less;
                else if (v->text == "<=") op = MachineOpcode::LessEqual;
                else if (v->text == ">") op = MachineOpcode::Greater;
                else if (v->text == ">=") op = MachineOpcode::GreaterEqual;
                else return fail("toán tử binary native chưa hỗ trợ: " + v->text);
                if (!value(v->operands[0], nesting + 1) ||
                    !value(v->operands[1], nesting + 1)) return false;
                add(op); --depth; return true;
            }
            case V::Call: {
                if (v->callTarget != compiler::CallTargetKind::DirectFunction ||
                    v->symbolId < 0 || v->operands.empty())
                    return fail("native chỉ hỗ trợ lời gọi hàm direct đã binding");
                const auto *callee = ir.value(v->operands.front());
                if (!callee || callee->opcode != V::LoadName ||
                    callee->symbolId != v->symbolId)
                    return fail("callee native không khớp semantic binding");
                const auto argc = v->operands.size() - 1;
                const compiler::IrInstruction *target = nullptr;
                for (const auto &inst : ir.instructions) {
                    if (inst.opcode != compiler::IrOpcode::DefineFunction ||
                        inst.symbolId != v->symbolId) continue;
                    if (target) return fail("native call có nhiều định nghĩa cùng symbol");
                    target = &inst;
                }
                const auto abiName = target ? nativeAbiSymbol(target->declarationName) : std::string{};
                if (!target || !symbolNameSafe(abiName) ||
                    target->parameters.size() != argc)
                    return fail("native call thiếu định nghĩa hàm/không đúng arity hoặc tên symbol");
                for (const auto &param : target->parameters)
                    if (param.hasDefault)
                        return fail("native call không hỗ trợ tham số mặc định");
                for (std::size_t i = 1; i < v->operands.size(); ++i)
                    if (!value(v->operands[i], nesting + 1)) return false;
                const auto call = add(MachineOpcode::Call, 0, argc);
                result.instructions[call].symbol = abiName;
                depth -= argc;
                push();
                return true;
            }
            default: return fail("IR native không hỗ trợ giá trị " +
                                 std::string(compiler::irValueOpcodeName(v->opcode)));
        }
    }

    bool expression(const compiler::IrInstruction &inst) {
        if (inst.expressionRoots.size() != 1 || !inst.children.empty())
            return fail("câu lệnh biểu thức native không hợp lệ");
        if (!value(inst.expressionRoots[0])) return false;
        drop(); return true;
    }

    bool statement(const compiler::IrInstruction &inst) {
        using I = compiler::IrOpcode;
        if (inst.unsupportedDirectRegion) return fail("unsupported direct IR region");
        switch (inst.opcode) {
            case I::NoOp:
                return inst.expressionRoots.empty() && inst.children.empty()
                    ? true : fail("NoOp IR chứa tác dụng phụ");
            case I::Block:
                if (!inst.expressionRoots.empty()) return fail("Block IR không hợp lệ");
                for (const auto &child : inst.children)
                    if (!statement(child)) return false;
                return true;
            case I::DeclareVariable: {
                if (!inst.children.empty() || inst.expressionRoots.size() > 1)
                    return fail("khai báo native không hợp lệ");
                if (inst.expressionRoots.empty()) return fail("native cần khởi tạo biến cục bộ");
                const auto found = slots.find(inst.symbolId);
                if (found == slots.end()) return fail("thiếu frame slot cho khai báo");
                if (!value(inst.expressionRoots.front())) return false;
                add(MachineOpcode::StoreSlot, 0, found->second);
                drop(); return true;
            }
            case I::Statement: return expression(inst);
            case I::Return:
                if (inst.expressionRoots.size() != 1 || !inst.children.empty())
                    return fail("native cần trả về đúng một giá trị i32");
                if (!value(inst.expressionRoots.front())) return false;
                add(MachineOpcode::Return); --depth;
                hasReturn = true; return true;
            case I::Conditional: {
                const bool otherwise = inst.conditionalForm ==
                    vietvm::frontend::AstConditionalForm::IfElseBlocks;
                if (inst.expressionRoots.size() != 1 ||
                    inst.children.size() != (otherwise ? 2u : 1u) ||
                    (inst.conditionalForm != vietvm::frontend::AstConditionalForm::IfBlock && !otherwise))
                    return fail("dạng điều kiện native chưa được hỗ trợ");
                if (!value(inst.expressionRoots.front())) return false;
                const auto toElse = add(MachineOpcode::BranchZero); --depth;
                if (!statement(inst.children.front())) return false;
                if (otherwise) {
                    const auto toEnd = add(MachineOpcode::Jump);
                    result.instructions[toElse].operand = result.instructions.size();
                    if (!statement(inst.children[1])) return false;
                    result.instructions[toEnd].operand = result.instructions.size();
                } else result.instructions[toElse].operand = result.instructions.size();
                return true;
            }
            case I::Loop: {
                const bool isWhile = inst.loopForm == vietvm::frontend::AstLoopForm::WhileBlock;
                if ((!isWhile && inst.loopForm != vietvm::frontend::AstLoopForm::ForBlock) ||
                    inst.expressionRoots.size() != (isWhile ? 1u : 3u) || inst.children.size() != 1)
                    return fail("native cần lặp(init; condition; update) { ... } hoặc khi(condition) { ... }");
                if (!isWhile) {
                    if (!value(inst.expressionRoots[0])) return false;
                    drop();
                }
                const auto start = result.instructions.size();
                if (!value(inst.expressionRoots[isWhile ? 0 : 1])) return false;
                const auto exit = add(MachineOpcode::BranchZero); --depth;
                loops.push_back({});
                if (!statement(inst.children.front())) return false;
                const auto update = result.instructions.size();
                if (!isWhile) {
                    if (!value(inst.expressionRoots[2])) return false;
                    drop();
                }
                add(MachineOpcode::Jump, 0, start);
                const auto end = result.instructions.size();
                result.instructions[exit].operand = end;
                for (auto jump : loops.back().breaks) result.instructions[jump].operand = end;
                for (auto jump : loops.back().continues) result.instructions[jump].operand = update;
                loops.pop_back();
                return true;
            }
            case I::Break:
            case I::Continue: {
                if (loops.empty()) return fail("thoát/bỏ qua ngoài vòng lặp native");
                const auto jump = add(MachineOpcode::Jump);
                if (inst.opcode == I::Break) loops.back().breaks.push_back(jump);
                else loops.back().continues.push_back(jump);
                return true;
            }
            default:
                return fail("IR native chưa hỗ trợ câu lệnh " +
                            std::string(compiler::irOpcodeName(inst.opcode)));
        }
    }
};

// Verify the evaluation stack across all CFG edges, including loop backedges
// and joins after conditional early returns. Unlike linear stack accounting,
// this rejects a jump into an instruction with an incompatible stack height.
// Validate unreachable code as well, since its bytes are still emitted.
std::string validateMachineStack(const MachineFunction &fn) {
    if (fn.instructions.empty()) return "machine IR không có lệnh";
    using Op = MachineOpcode;
    const auto n = fn.instructions.size();
    constexpr auto unknown = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> incoming(n, unknown);
    std::deque<std::size_t> pending;
    incoming[0] = 0;
    pending.push_back(0);

    const auto transfer = [&](std::size_t i, std::size_t &after) -> std::string {
        const auto &inst = fn.instructions[i];
        const auto before = inst.depthBefore;
        if (before > fn.maxStackDepth) return "machine IR stack vượt frame";
        after = before;
        switch (inst.opcode) {
            case Op::PushConstant:
                if (before >= fn.maxStackDepth) return "machine IR stack overflow";
                ++after; break;
            case Op::LoadSlot:
                if (inst.operand >= fn.slotCount || before >= fn.maxStackDepth)
                    return "machine IR load slot ngoài frame";
                ++after; break;
            case Op::StoreSlot:
                if (inst.operand >= fn.slotCount || before == 0)
                    return "machine IR store slot ngoài frame";
                break;
            case Op::Drop:
            case Op::BranchZero:
            case Op::Return:
                if (before == 0) return "machine IR stack underflow";
                --after; break;
            case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
            case Op::Equal: case Op::NotEqual: case Op::Less: case Op::LessEqual:
            case Op::Greater: case Op::GreaterEqual:
                if (before < 2) return "machine IR binary stack underflow";
                --after; break;
            case Op::Call:
                if (before < inst.operand ||
                    !symbolNameSafe(inst.symbol) ||
                    before - inst.operand >= fn.maxStackDepth)
                    return "machine IR call/stack/symbol không hợp lệ";
                after = before - inst.operand + 1;
                break;
            case Op::Jump: case Op::Trap: break;
            default: return "machine IR opcode không hợp lệ";
        }
        if ((inst.opcode == Op::Jump || inst.opcode == Op::BranchZero) &&
            inst.operand >= n) return "machine IR branch ngoài phạm vi";
        if (inst.opcode == Op::Return && after != 0)
            return "machine IR return phải tiêu thụ đúng một giá trị";
        return {};
    };

    for (std::size_t i = 0; i < n; ++i) {
        std::size_t after = 0;
        if (const auto error = transfer(i, after); !error.empty()) return error;
    }

    while (!pending.empty()) {
        const auto i = pending.front();
        pending.pop_front();
        const auto &inst = fn.instructions[i];
        if (incoming[i] != inst.depthBefore)
            return "machine IR stack depth sai tại CFG join/branch";
        std::size_t after = 0;
        if (const auto error = transfer(i, after); !error.empty()) return error;
        const auto connect = [&](std::size_t dest) -> bool {
            if (incoming[dest] == unknown) {
                incoming[dest] = after;
                pending.push_back(dest);
            }
            return incoming[dest] == after;
        };
        if (inst.opcode == Op::Return || inst.opcode == Op::Trap) continue;
        if ((inst.opcode == Op::Jump || inst.opcode == Op::BranchZero) &&
            !connect(inst.operand)) return "machine IR stack không khớp tại đích nhảy";
        if (inst.opcode != Op::Jump) {
            if (i + 1 == n) return "machine IR fallthrough vượt cuối hàm";
            if (!connect(i + 1)) return "machine IR stack không khớp tại CFG join";
        }
    }
    return {};
}

} // namespace

MachineFunctionLowering lowerMachineFunction(const compiler::IrProgram &ir,
                                              std::string_view functionName) {
    const compiler::IrInstruction *function = nullptr;
    for (const auto &inst : ir.instructions) {
        if (inst.opcode == compiler::IrOpcode::DefineFunction &&
            inst.declarationName == functionName) {
            if (function) return {{}, "có nhiều hàm cùng tên"};
            function = &inst;
        }
    }
    if (!function) return {{}, "không tìm thấy hàm: " + std::string(functionName)};
    return MachineLowerer(ir).run(*function);
}

MachineEmission emitMachineFunction(const Target &target,
                                    const MachineFunction &function) {
    if (target.arch == Architecture::AArch64 && target.os == OperatingSystem::Windows)
        return {{}, {}, "Windows AArch64 ABI chưa được triển khai", {}};
    if (const auto error = validateMachineStack(function); !error.empty())
        return {{}, {}, error, {}};
    BackendEmission out;
    switch (target.arch) {
        case Architecture::X86_64: out = emitX86_64Function(function, target); break;
        case Architecture::AArch64: out = emitAArch64Function(function, target); break;
        default: return {{}, {}, "Machine IR chỉ hỗ trợ x86_64/AArch64", {}};
    }
    return {std::move(out.bytes), std::move(out.assembly),
            std::move(out.error), std::move(out.relocations)};
}

RawAssemblyResult emitMachineAssembly(const Target &target,
                                      const MachineEmission &emission,
                                      std::string_view symbol) {
    const auto abiName = nativeAbiSymbol(symbol);
    if (!symbolNameSafe(abiName)) return {{}, "tên nhãn assembly không hợp lệ"};
    if (!emission) return {{}, "machine IR chưa tạo mã máy"};
    const auto label = (target.os == OperatingSystem::MacOS ? "_" : "") +
                       abiName;
    const std::string dialect = target.arch == Architecture::X86_64
        ? ".intel_syntax noprefix\n" : "";
    std::string body = emission.assembly;
    if (target.arch == Architecture::X86_64) {
        // GAS/Clang may shorten jumps and memory displacements in mnemonic
        // assembly. The native V++ emitter uses fixed-width encoded bytes;
        // preserve the same instructions, leaving CALL operands relocatable.
        body.clear();
        std::size_t cursor = 0;
        const auto appendBytes = [&](std::size_t end) {
            while (cursor < end) {
                const auto count = std::min<std::size_t>(16, end - cursor);
                body += ".byte ";
                for (std::size_t i = 0; i < count; ++i) {
                    if (i) body += ", ";
                    body += std::to_string(emission.bytes[cursor++]);
                }
                body += '\n';
            }
        };
        for (const auto &relocation : emission.relocations) {
            if (relocation.offset <= cursor || relocation.offset + 4 > emission.bytes.size() ||
                emission.bytes[relocation.offset - 1] != 0xe8 ||
                !symbolNameSafe(relocation.symbol))
                return {{}, "x86_64: vị trí hoặc tên relocation không hợp lệ"};
            appendBytes(relocation.offset - 1);
            body += "call ";
            if (target.os == OperatingSystem::MacOS) body += '_';
            body += relocation.symbol + '\n';
            cursor = relocation.offset + 4;
        }
        appendBytes(emission.bytes.size());
    }
    return {dialect + ".text\n.globl " + label + "\n" + label + ":\n" +
            body, {}};
}

} // namespace vietvm::native
