#include "raw_backend.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace vietvm::native {

// Standalone aarch64 machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitAArch64(const RawLeaf &leaf, const Target &target) {
    (void)target;
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        const auto bits = static_cast<std::uint32_t>(leaf.constant);
        appendWord(out.bytes, 0x52800000u | ((bits & 0xffffu) << 5), true); // MOVZ W0
        appendWord(out.bytes, 0x72a00000u | ((bits >> 16) << 5), true);    // MOVK W0,LSL #16
        appendWord(out.bytes, 0xd65f03c0u, true); // RET
        out.assembly = "movz w0, #" + std::to_string(bits & 0xffffu) +
            "\nmovk w0, #" + std::to_string(bits >> 16) + ", lsl #16\nret\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        appendWord(out.bytes, 0x0b010000u, true); // ADD W0,W0,W1
        appendWord(out.bytes, 0xd65f03c0u, true); // RET
        out.assembly = "add w0, w0, w1\nret\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0x4b010000u, true); // SUB W0,W0,W1
        appendWord(out.bytes, 0xd65f03c0u, true);
        out.assembly = "sub w0, w0, w1\nret\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

BackendEmission emitAArch64Function(const MachineFunction &fn, const Target &target) {
    BackendEmission out;
    if (fn.slotCount < fn.parameterCount ||
        fn.slotCount > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8) ||
        fn.maxStackDepth > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8) ||
        fn.slotCount + fn.maxStackDepth >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 4) - 4 ||
        fn.instructions.empty()) {
        out.error = "AArch64 machine IR/frame không hợp lệ";
        return out;
    }
    auto &code = out.bytes;
    auto &assembly = out.assembly;
    // A fixed register per local/evaluation slot also works at CFG joins and
    // back edges: no path-dependent cache state needs to be reconciled. Only
    // leaf functions qualify, since these registers are caller-clobbered.
    // W9/W10 remain scratch; W18 (platform register) and W19+ are never used.
    const bool registerSlots = fn.parameterCount <= 8 &&
        fn.slotCount + fn.maxStackDepth <= 16 &&
        std::none_of(fn.instructions.begin(), fn.instructions.end(),
                     [](const auto &op) { return op.opcode == MachineOpcode::Call; });
    const auto slotRegister = [](std::size_t slot) {
        return static_cast<unsigned>(slot < 9 ? slot : slot + 2);
    };
    // CMP followed by BranchZero can branch on the flags directly, provided
    // the branch is not independently reachable through another jump edge.
    std::vector<bool> branchTargets(fn.instructions.size(), false);
    for (const auto &op : fn.instructions) {
        if ((op.opcode == MachineOpcode::BranchZero || op.opcode == MachineOpcode::Jump) &&
            op.operand < branchTargets.size()) {
            branchTargets[op.operand] = true;
        }
    }
    std::vector<int> fusedConditions(fn.instructions.size(), -1);
    for (std::size_t i = 0; i + 1 < fn.instructions.size(); ++i) {
        const auto &op = fn.instructions[i];
        const auto &next = fn.instructions[i + 1];
        int inverse = -1;
        switch (op.opcode) {
            case MachineOpcode::Equal: inverse = 1; break;
            case MachineOpcode::NotEqual: inverse = 0; break;
            case MachineOpcode::Less: inverse = 10; break;
            case MachineOpcode::LessEqual: inverse = 12; break;
            case MachineOpcode::Greater: inverse = 13; break;
            case MachineOpcode::GreaterEqual: inverse = 11; break;
            default: break;
        }
        if (inverse >= 0 && op.depthBefore >= 2 &&
            next.opcode == MachineOpcode::BranchZero &&
            next.depthBefore == op.depthBefore - 1 && !branchTargets[i + 1]) {
            fusedConditions[i] = inverse;
            fusedConditions[i + 1] = inverse;
        }
    }
    std::vector<bool> skipLoads(fn.instructions.size(), false);
    std::vector<std::pair<int, int>> directSources(fn.instructions.size(), {-1, -1});
    std::vector<int> directImmediates(fn.instructions.size(), -1);
    std::vector<bool> directArithmeticConstant(fn.instructions.size(), false);
    std::vector<std::int32_t> directArithmeticValues(fn.instructions.size(), 0);
    std::vector<bool> directCompareConstant(fn.instructions.size(), false);
    std::vector<std::int32_t> directCompareValues(fn.instructions.size(), 0);
    if (registerSlots) {
        for (std::size_t i = 0; i + 2 < fn.instructions.size(); ++i) {
            const auto &a = fn.instructions[i];
            const auto &b = fn.instructions[i + 1];
            const auto &op = fn.instructions[i + 2];
            const bool binary = op.opcode == MachineOpcode::Add ||
                op.opcode == MachineOpcode::Sub || op.opcode == MachineOpcode::Mul ||
                op.opcode == MachineOpcode::Equal || op.opcode == MachineOpcode::NotEqual ||
                op.opcode == MachineOpcode::Less || op.opcode == MachineOpcode::LessEqual ||
                op.opcode == MachineOpcode::Greater || op.opcode == MachineOpcode::GreaterEqual;
            if (a.opcode == MachineOpcode::LoadSlot && b.opcode == MachineOpcode::LoadSlot &&
                binary && !branchTargets[i + 1] && !branchTargets[i + 2] &&
                a.operand < fn.slotCount && b.operand < fn.slotCount &&
                a.depthBefore + 1 < fn.maxStackDepth &&
                b.depthBefore == a.depthBefore + 1 &&
                op.depthBefore == b.depthBefore + 1) {
                skipLoads[i] = true;
                skipLoads[i + 1] = true;
                directSources[i + 2] = {static_cast<int>(a.operand),
                                        static_cast<int>(b.operand)};
            }
            if (a.opcode == MachineOpcode::LoadSlot && b.opcode == MachineOpcode::PushConstant &&
                (op.opcode == MachineOpcode::Add || op.opcode == MachineOpcode::Sub) &&
                !branchTargets[i + 1] && !branchTargets[i + 2] &&
                a.operand < fn.slotCount && b.immediate >= 0 && b.immediate <= 4095 &&
                a.depthBefore + 1 < fn.maxStackDepth &&
                b.depthBefore == a.depthBefore + 1 &&
                op.depthBefore == b.depthBefore + 1) {
                skipLoads[i] = true;
                skipLoads[i + 1] = true;
                directSources[i + 2].first = static_cast<int>(a.operand);
                directImmediates[i + 2] = static_cast<int>(b.immediate);
            }
            if (a.opcode == MachineOpcode::LoadSlot && b.opcode == MachineOpcode::PushConstant &&
                (op.opcode == MachineOpcode::Add || op.opcode == MachineOpcode::Sub) &&
                (b.immediate < 0 || b.immediate > 4095) &&
                !branchTargets[i + 1] && !branchTargets[i + 2] &&
                a.operand < fn.slotCount && a.depthBefore + 1 < fn.maxStackDepth &&
                b.depthBefore == a.depthBefore + 1 &&
                op.depthBefore == b.depthBefore + 1) {
                skipLoads[i] = true;
                skipLoads[i + 1] = true;
                directSources[i + 2].first = static_cast<int>(a.operand);
                directArithmeticConstant[i + 2] = true;
                directArithmeticValues[i + 2] = b.immediate;
            }
            const bool comparison = op.opcode == MachineOpcode::Equal ||
                op.opcode == MachineOpcode::NotEqual || op.opcode == MachineOpcode::Less ||
                op.opcode == MachineOpcode::LessEqual || op.opcode == MachineOpcode::Greater ||
                op.opcode == MachineOpcode::GreaterEqual;
            if (a.opcode == MachineOpcode::LoadSlot && b.opcode == MachineOpcode::PushConstant &&
                comparison && !branchTargets[i + 1] && !branchTargets[i + 2] &&
                a.operand < fn.slotCount && a.depthBefore + 1 < fn.maxStackDepth &&
                b.depthBefore == a.depthBefore + 1 &&
                op.depthBefore == b.depthBefore + 1) {
                skipLoads[i] = true;
                skipLoads[i + 1] = true;
                directSources[i + 2].first = static_cast<int>(a.operand);
                directCompareConstant[i + 2] = true;
                directCompareValues[i + 2] = b.immediate;
            }
        }
    }
    // W10 is never allocated to a slot. Leaf mode has no calls, and other
    // scratch operations use W9. Keep one constant shared by at least two
    // operations in a loop live through its back edge.
    std::vector<bool> inLoop(fn.instructions.size(), false);
    for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
        const auto &op = fn.instructions[i];
        if ((op.opcode == MachineOpcode::Jump || op.opcode == MachineOpcode::BranchZero) &&
            op.operand < i) {
            for (auto j = op.operand; j <= i; ++j) inLoop[j] = true;
        }
    }
    bool hasHoistedConstant = false;
    std::int32_t hoistedConstant = 0;
    if (registerSlots) {
        for (std::size_t i = 0; i < fn.instructions.size() && !hasHoistedConstant; ++i) {
            const bool candidate = directArithmeticConstant[i] ||
                (directCompareConstant[i] &&
                 (directCompareValues[i] < 0 || directCompareValues[i] > 4095));
            if (!candidate || !inLoop[i]) continue;
            const auto value = directArithmeticConstant[i] ?
                directArithmeticValues[i] : directCompareValues[i];
            std::size_t uses = 0;
            for (std::size_t j = 0; j < fn.instructions.size(); ++j) {
                if (inLoop[j] && ((directArithmeticConstant[j] && directArithmeticValues[j] == value) ||
                                  (directCompareConstant[j] && directCompareValues[j] == value &&
                                   (value < 0 || value > 4095)))) ++uses;
            }
            if (uses >= 2) { hasHoistedConstant = true; hoistedConstant = value; }
        }
    }
    std::vector<bool> skipStores(fn.instructions.size(), false);
    std::vector<int> directDestination(fn.instructions.size(), -1);
    std::vector<int> copySource(fn.instructions.size(), -1);
    std::vector<int> returnSource(fn.instructions.size(), -1);
    if (registerSlots) {
        for (std::size_t i = 0; i + 2 < fn.instructions.size(); ++i) {
            const auto &a = fn.instructions[i];
            const auto &b = fn.instructions[i + 1];
            const auto &c = fn.instructions[i + 2];
            const bool producesValue = a.opcode == MachineOpcode::PushConstant ||
                a.opcode == MachineOpcode::Add || a.opcode == MachineOpcode::Sub ||
                a.opcode == MachineOpcode::Mul || a.opcode == MachineOpcode::Equal ||
                a.opcode == MachineOpcode::NotEqual || a.opcode == MachineOpcode::Less ||
                a.opcode == MachineOpcode::LessEqual || a.opcode == MachineOpcode::Greater ||
                a.opcode == MachineOpcode::GreaterEqual;
            if (b.opcode == MachineOpcode::StoreSlot && c.opcode == MachineOpcode::Drop &&
                !branchTargets[i + 1] && !branchTargets[i + 2] &&
                b.operand < fn.slotCount && b.depthBefore == c.depthBefore &&
                b.depthBefore >= 1) {
                if (producesValue &&
                    ((a.opcode == MachineOpcode::PushConstant &&
                      a.depthBefore + 1 == b.depthBefore && a.depthBefore < fn.maxStackDepth) ||
                     (a.opcode != MachineOpcode::PushConstant &&
                      a.depthBefore >= 2 && a.depthBefore - 1 == b.depthBefore))) {
                    directDestination[i] = static_cast<int>(b.operand);
                    skipStores[i + 1] = true;
                }
                if (a.opcode == MachineOpcode::LoadSlot &&
                    a.depthBefore + 1 == b.depthBefore &&
                    a.operand < fn.slotCount && a.depthBefore < fn.maxStackDepth &&
                    !skipLoads[i]) {
                    skipLoads[i] = true;
                    copySource[i + 1] = static_cast<int>(a.operand);
                }
            }
        }
        for (std::size_t i = 0; i + 1 < fn.instructions.size(); ++i) {
            const auto &a = fn.instructions[i];
            const auto &b = fn.instructions[i + 1];
            if (a.opcode == MachineOpcode::LoadSlot && b.opcode == MachineOpcode::Return &&
                !branchTargets[i + 1] && !skipLoads[i] &&
                a.operand < fn.slotCount && a.depthBefore < fn.maxStackDepth &&
                b.depthBefore == a.depthBefore + 1) {
                skipLoads[i] = true;
                returnSource[i + 1] = static_cast<int>(a.operand);
            }
        }
    }
    const auto frameSize = registerSlots ? 0u : static_cast<std::uint32_t>(
        (4 * (fn.slotCount + fn.maxStackDepth) + 15) & ~std::size_t(15));
    // Darwin ARM64 packs stack-passed i32s at their natural 4-byte alignment.
    // Linux AAPCS64 uses 8-byte stack slots for these integer arguments.
    const std::uint32_t stackArgStride = target.os == OperatingSystem::MacOS ? 4u : 8u;
    const auto word = [&](std::uint32_t bits) { appendWord(code, bits, true); };
    const auto adjustSP = [&](std::uint32_t bytes, bool subtract) {
        while (bytes) {
            const auto step = std::min(bytes, 4095u);
            word((subtract ? 0xd10003ffu : 0x910003ffu) | (step << 10));
            assembly += std::string(subtract ? "sub" : "add") +
                " sp, sp, #" + std::to_string(step) + "\n";
            bytes -= step;
        }
    };
    const auto move = [&](unsigned destination, unsigned source) {
        if (destination == source) return;
        word(0x2a0003e0u | (source << 16) | destination);
        assembly += "mov w" + std::to_string(destination) + ", w" + std::to_string(source) + "\n";
    };
    const auto loadConstant = [&](unsigned reg, std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        word(0x52800000u | ((bits & 0xffffu) << 5) | reg);
        assembly += "movz w" + std::to_string(reg) + ", #" + std::to_string(bits & 0xffffu) + "\n";
        if (bits >> 16) {
            word(0x72a00000u | ((bits >> 16) << 5) | reg);
            assembly += "movk w" + std::to_string(reg) + ", #" + std::to_string(bits >> 16) + ", lsl #16\n";
        }
    };
    const auto stackAccess = [&](bool load, unsigned reg, std::uint32_t offset) {
        const auto prefix = std::string(load ? "ldr w" : "str w") +
            std::to_string(reg) + ", ";
        if (offset <= 16380 && (offset % 4) == 0) {
            word((load ? 0xb9400000u : 0xb9000000u) |
                 ((offset / 4) << 10) | (31u << 5) | reg);
            assembly += prefix + "[sp, #" + std::to_string(offset) + "]\n";
        } else {
            word(0x910003ebu); // add x11, sp, #0
            assembly += "add x11, sp, #0\n";
            auto remaining = offset;
            while (remaining) {
                const auto step = std::min(remaining, 4095u);
                word(0x9100016bu | (step << 10));
                assembly += "add x11, x11, #" + std::to_string(step) + "\n";
                remaining -= step;
            }
            word((load ? 0xb9400160u : 0xb9000160u) | reg);
            assembly += prefix + "[x11]\n";
        }
    };
    const auto access = [&](bool load, unsigned reg, std::size_t slot) {
        if (registerSlots) {
            if (load) move(reg, slotRegister(slot));
            else move(slotRegister(slot), reg);
        } else {
            stackAccess(load, reg, static_cast<std::uint32_t>(slot * 4));
        }
    };
    // Register-only leaves need neither a frame nor an LR save. The fallback
    // retains its fixed aligned frame, including across nested calls.
    if (!registerSlots) {
        word(0xa9bf7bfdu); word(0x910003fdu); // stp x29,x30; mov x29,sp
        assembly = "stp x29, x30, [sp, #-16]!\nmov x29, sp\n";
    }
    if (frameSize) {
        adjustSP(frameSize, true);
    }
    for (std::size_t i = 0; i < fn.parameterCount; ++i) {
        if (i < 8) access(false, static_cast<unsigned>(i), i);
        else {
            // Arguments beyond x0..x7 are loaded from the caller's ABI stack.
            stackAccess(true, 9, frameSize + 16u +
                        static_cast<std::uint32_t>((i - 8) * stackArgStride));
            access(false, 9, i);
        }
    }
    if (hasHoistedConstant) loadConstant(10, hoistedConstant);

    struct Fixup { std::size_t offset, target; bool conditional; };
    std::vector<Fixup> fixups;
    std::vector<std::size_t> offsets(fn.instructions.size());
    const auto eval = [&](std::size_t depth) { return fn.slotCount + depth; };
    const auto fail = [&](const char *reason) {
        out.bytes.clear(); out.error = reason; return out;
    };
    using Op = MachineOpcode;
    for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
        std::size_t depth = fn.instructions[i].depthBefore;
        offsets[i] = code.size();
        assembly += ".L_vpp_" + std::to_string(i) + ":\n";
        const auto &op = fn.instructions[i];
        if (skipLoads[i] || skipStores[i]) continue;
        switch (op.opcode) {
            case Op::PushConstant: {
                if (depth >= fn.maxStackDepth) return fail("ARM64 eval stack overflow");
                const auto bits = static_cast<std::uint32_t>(op.immediate);
                const auto reg = directDestination[i] >= 0 ?
                    slotRegister(directDestination[i]) :
                    (registerSlots ? slotRegister(eval(depth)) : 9u);
                word(0x52800000u | ((bits & 0xffffu) << 5) | reg);
                assembly += "movz w" + std::to_string(reg) + ", #" + std::to_string(bits & 0xffffu) + "\n";
                if (!registerSlots || (bits >> 16)) {
                    word(0x72a00000u | ((bits >> 16) << 5) | reg);
                    assembly += "movk w" + std::to_string(reg) + ", #" + std::to_string(bits >> 16) + ", lsl #16\n";
                }
                if (directDestination[i] < 0) access(false, reg, eval(depth));
                break;
            }
            case Op::LoadSlot:
                if (op.operand >= fn.slotCount || depth >= fn.maxStackDepth)
                    return fail("ARM64 load slot ngoài frame");
                if (registerSlots) move(slotRegister(eval(depth)), slotRegister(op.operand));
                else {
                    access(true, 9, op.operand);
                    access(false, 9, eval(depth));
                }
                ++depth; break;
            case Op::StoreSlot:
                if (!depth || op.operand >= fn.slotCount)
                    return fail("ARM64 store slot ngoài frame");
                if (copySource[i] >= 0) move(slotRegister(op.operand), slotRegister(copySource[i]));
                else if (registerSlots) move(slotRegister(op.operand), slotRegister(eval(depth - 1)));
                else {
                    access(true, 9, eval(depth - 1));
                    access(false, 9, op.operand);
                }
                break;
            case Op::Drop:
                if (!depth) return fail("ARM64 eval stack underflow");
                --depth; break;
            // Legacy C++ oracle remains restricted; production i32 division
            // is emitted directly by the V++ backend.
            case Op::Div: case Op::Mod:
                return fail("ARM64 Div/Mod: dùng backend V++ native");
            case Op::Add: case Op::Sub: case Op::Mul:
            case Op::Equal: case Op::NotEqual: case Op::Less: case Op::LessEqual:
            case Op::Greater: case Op::GreaterEqual: {
                if (depth < 2) return fail("ARM64 binary stack underflow");
                const auto destination = directDestination[i] >= 0 ?
                    slotRegister(directDestination[i]) :
                    (registerSlots ? slotRegister(eval(depth - 2)) : 9u);
                if (directImmediates[i] >= 0) {
                    const auto source = slotRegister(directSources[i].first);
                    word((op.opcode == Op::Add ? 0x11000000u : 0x51000000u) |
                         (static_cast<unsigned>(directImmediates[i]) << 10) |
                         (source << 5) | destination);
                    assembly += std::string(op.opcode == Op::Add ? "add w" : "sub w") +
                        std::to_string(destination) + ", w" + std::to_string(source) +
                        ", #" + std::to_string(directImmediates[i]) + "\n";
                    break;
                }
                if (directArithmeticConstant[i]) {
                    const auto source = slotRegister(directSources[i].first);
                    const auto value = directArithmeticValues[i];
                    const auto constantReg = hasHoistedConstant && value == hoistedConstant ? 10u : 9u;
                    if (constantReg == 9) loadConstant(9, value);
                    word((op.opcode == Op::Add ? 0x0b000000u : 0x4b000000u) |
                         (constantReg << 16) | (source << 5) | destination);
                    assembly += std::string(op.opcode == Op::Add ? "add w" : "sub w") +
                        std::to_string(destination) + ", w" + std::to_string(source) +
                        ", w" + std::to_string(constantReg) + "\n";
                    break;
                }
                const bool compareConstant = directCompareConstant[i];
                const auto constant = directCompareValues[i];
                const bool compareImm12 = compareConstant && constant >= 0 && constant <= 4095;
                const auto constantReg = hasHoistedConstant && constant == hoistedConstant ? 10u : 9u;
                if (compareConstant && !compareImm12 && constantReg == 9) loadConstant(9, constant);
                const bool direct = directSources[i].first >= 0 &&
                    (directSources[i].second >= 0 || compareConstant);
                const auto left = direct ? slotRegister(directSources[i].first) :
                    (registerSlots ? slotRegister(eval(depth - 2)) : 9u);
                const auto right = compareConstant ? constantReg : direct ? slotRegister(directSources[i].second) :
                    (registerSlots ? slotRegister(eval(depth - 1)) : 10u);
                if (!direct) {
                    access(true, left, eval(depth - 2));
                    access(true, right, eval(depth - 1));
                }
                const auto operands = (right << 16) | (left << 5) | destination;
                const auto registers = " w" + std::to_string(destination) + ", w" +
                    std::to_string(left) + ", w" + std::to_string(right) + "\n";
                if (op.opcode == Op::Add) {
                    word(0x0b000000u | operands); assembly += "add" + registers;
                } else if (op.opcode == Op::Sub) {
                    word(0x4b000000u | operands); assembly += "sub" + registers;
                } else if (op.opcode == Op::Mul) {
                    word(0x1b007c00u | operands); assembly += "mul" + registers;
                } else {
                    if (compareImm12) {
                        word(0x7100001fu | (static_cast<unsigned>(constant) << 10) | (left << 5));
                        assembly += "cmp w" + std::to_string(left) + ", #" +
                            std::to_string(constant) + "\n";
                    } else {
                        word(0x6b00001fu | (right << 16) | (left << 5));
                        assembly += "cmp w" + std::to_string(left) + ", w" + std::to_string(right) + "\n";
                    }
                    unsigned cond = 0;
                    const char *condition = "";
                    switch (op.opcode) {
                        case Op::Equal: cond = 0; condition = "eq"; break;
                        case Op::NotEqual: cond = 1; condition = "ne"; break;
                        case Op::Less: cond = 11; condition = "lt"; break;
                        case Op::LessEqual: cond = 13; condition = "le"; break;
                        case Op::Greater: cond = 12; condition = "gt"; break;
                        default: cond = 10; condition = "ge"; break;
                    }
                    // CSET is CSINC(WZR,WZR,inverse condition).
                    if (fusedConditions[i] < 0) {
                        word(0x1a9f07e0u | ((cond ^ 1u) << 12) | destination);
                        assembly += "cset w" + std::to_string(destination) + ", " + condition + "\n";
                    }
                }
                if (fusedConditions[i] < 0 && directDestination[i] < 0)
                    access(false, destination, eval(depth - 2));
                break;
            }
            case Op::BranchZero: {
                if (!depth || op.operand >= fn.instructions.size())
                    return fail("ARM64 branch không hợp lệ");
                fixups.push_back({code.size(), op.operand, true});
                if (fusedConditions[i] >= 0) {
                    const auto cond = static_cast<unsigned>(fusedConditions[i]);
                    word(0x54000000u | cond);
                    constexpr const char *names[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                                     "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"};
                    assembly += "b." + std::string(names[cond]) + " .L_vpp_" +
                        std::to_string(op.operand) + "\n";
                } else {
                    const auto reg = registerSlots ? slotRegister(eval(depth - 1)) : 9u;
                    access(true, reg, eval(depth - 1));
                    word(0x34000000u | reg);
                    assembly += "cbz w" + std::to_string(reg) + ", .L_vpp_" +
                        std::to_string(op.operand) + "\n";
                }
                break;
            }
            case Op::Jump:
                if (op.operand >= fn.instructions.size())
                    return fail("ARM64 jump không hợp lệ");
                fixups.push_back({code.size(), op.operand, false});
                word(0x14000000u);
                assembly += "b .L_vpp_" + std::to_string(op.operand) + "\n"; break;
            case Op::Call: {
                if (depth < op.operand || op.symbol.empty())
                    return fail("ARM64 call thiếu arguments/symbol");
                for (std::size_t n = 0; n < std::min<std::size_t>(op.operand, 8); ++n)
                    access(true, static_cast<unsigned>(n), eval(depth - op.operand + n));
                const auto extra = op.operand > 8 ? op.operand - 8 : 0;
                if (extra > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / stackArgStride) - 4)
                    return fail("ARM64 call stack quá lớn");
                const auto outgoing = static_cast<std::uint32_t>((extra * stackArgStride + 15) & ~std::size_t(15));
                adjustSP(outgoing, true);
                for (std::size_t n = 8; n < op.operand; ++n) {
                    stackAccess(true, 9, outgoing +
                        static_cast<std::uint32_t>(eval(depth - op.operand + n) * 4));
                    stackAccess(false, 9, static_cast<std::uint32_t>((n - 8) * stackArgStride));
                }
                out.relocations.push_back({code.size(), op.symbol});
                word(0x94000000u); // BL imm26, linked by object relocation
                assembly += std::string("bl ") + (target.os == OperatingSystem::MacOS ? "_" : "") +
                            op.symbol + "\n";
                adjustSP(outgoing, false);
                access(false, 0, eval(depth - op.operand));
                break;
            }
            case Op::Return:
                if (!depth) return fail("ARM64 return không có giá trị");
                if (returnSource[i] >= 0) move(0, slotRegister(returnSource[i]));
                else access(true, 0, eval(depth - 1));
                if (frameSize) {
                    adjustSP(frameSize, false);
                }
                if (!registerSlots) {
                    word(0xa8c17bfdu); assembly += "ldp x29, x30, [sp], #16\n";
                }
                word(0xd65f03c0u); assembly += "ret\n";
                break;
            case Op::Trap:
                word(0xd4200000u); assembly += "brk #0\n"; break;
        }
    }
    for (const auto &fix : fixups) {
        const auto displacement = static_cast<std::int64_t>(offsets[fix.target]) -
                                  static_cast<std::int64_t>(fix.offset);
        const auto words = displacement / 4;
        if (displacement % 4 ||
            (fix.conditional && (words < -(1 << 18) || words >= (1 << 18))) ||
            (!fix.conditional && (words < -(1 << 25) || words >= (1 << 25))))
            return fail("ARM64 branch vượt giới hạn ISA");
        const auto bits = static_cast<std::uint32_t>(words);
        const std::uint32_t old =
            static_cast<std::uint32_t>(code[fix.offset]) |
            (static_cast<std::uint32_t>(code[fix.offset + 1]) << 8) |
            (static_cast<std::uint32_t>(code[fix.offset + 2]) << 16) |
            (static_cast<std::uint32_t>(code[fix.offset + 3]) << 24);
        const auto patched = old | (fix.conditional ? ((bits & 0x7ffffu) << 5)
                                                : (bits & 0x03ffffffu));
        for (unsigned n = 0; n < 4; ++n)
            code[fix.offset + n] = static_cast<std::uint8_t>(patched >> (8 * n));
    }
    return out;
}

} // namespace vietvm::native
