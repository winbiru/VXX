#include "raw_backend.h"

#include <cstdint>
#include <limits>
#include <string>

namespace vietvm::native {

// Standalone x86_64 machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitX86_64(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        out.bytes.push_back(0xb8); // MOV EAX,imm32
        appendWord(out.bytes, static_cast<std::uint32_t>(leaf.constant), true);
        out.bytes.push_back(0xc3); // RET
        out.assembly = "mov eax, " + std::to_string(leaf.constant) + "\nret\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        if (target.os == OperatingSystem::Windows) {
            out.bytes = {0x89, 0xc8, 0x01, 0xd0, 0xc3}; // ECX,EDX (Win64)
            out.assembly = "mov eax, ecx\nadd eax, edx\nret\n";
        } else {
            out.bytes = {0x89, 0xf8, 0x01, 0xf0, 0xc3}; // EDI,ESI (SysV)
            out.assembly = "mov eax, edi\nadd eax, esi\nret\n";
        }
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        if (target.os == OperatingSystem::Windows) {
            out.bytes = {0x89, 0xc8, 0x29, 0xd0, 0xc3}; // ECX-EDX (Win64)
            out.assembly = "mov eax, ecx\nsub eax, edx\nret\n";
        } else {
            out.bytes = {0x89, 0xf8, 0x29, 0xf0, 0xc3}; // EDI-ESI (SysV)
            out.assembly = "mov eax, edi\nsub eax, esi\nret\n";
        }
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

BackendEmission emitX86_64Function(const MachineFunction &fn, const Target &target) {
    BackendEmission out;
    if (fn.slotCount < fn.parameterCount ||
        fn.slotCount > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8) ||
        fn.maxStackDepth > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8) ||
        fn.slotCount + fn.maxStackDepth >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 4) - 16 ||
        fn.instructions.empty()) {
        out.error = "x86_64 machine IR/frame không hợp lệ";
        return out;
    }
    const bool win64 = target.os == OperatingSystem::Windows;
    // Win64 requires 32 bytes of outgoing shadow space at rsp for every call.
    const auto frameSize = static_cast<std::uint32_t>(
        (4 * (fn.slotCount + fn.maxStackDepth) + (win64 ? 32 : 0) + 15) & ~std::size_t(15));
    auto &code = out.bytes;
    auto &assembly = out.assembly;
    const auto byte = [&](std::uint8_t value) { code.push_back(value); };
    const auto word = [&](std::uint32_t value) { appendWord(code, value, true); };
    const auto slotOffset = [](std::size_t slot) {
        return static_cast<std::uint32_t>(4 * (slot + 1));
    };
    // Use disp32 consistently so the byte encoder and assembler share one
    // unambiguous signed displacement independent of slot index.
    const auto memory = [&](bool load, unsigned reg, std::size_t slot) {
        byte(load ? 0x8b : 0x89);
        byte(static_cast<std::uint8_t>(0x85 | (reg << 3)));
        word(0u - slotOffset(slot));
    };
    const auto memName = [&](std::size_t slot) {
        return "dword ptr [rbp - " + std::to_string(slotOffset(slot)) + "]";
    };
    byte(0x55); // push rbp
    byte(0x48); byte(0x89); byte(0xe5); // mov rbp, rsp
    assembly = "push rbp\nmov rbp, rsp\n";
    if (frameSize) {
        byte(0x48); byte(0x81); byte(0xec); word(frameSize);
        assembly += "sub rsp, " + std::to_string(frameSize) + "\n";
    }
    // SysV: edi,esi,edx,ecx,r8d,r9d; Win64: ecx,edx,r8d,r9d.
    const std::string sysRegs[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
    const std::string winRegs[] = {"ecx", "edx", "r8d", "r9d"};
    const unsigned sysEnc[] = {7, 6, 2, 1, 0, 1};
    const unsigned winEnc[] = {1, 2, 0, 1};
    const auto regArgs = win64 ? 4u : 6u;
    const auto stackIncoming = [&](std::size_t i) {
        byte(0x8b); byte(0x85); // mov eax, [rbp+disp32]
        const auto offset = static_cast<std::uint32_t>(
            (win64 ? 48 : 16) + 8 * (i - regArgs));
        word(offset);
        assembly += "mov eax, dword ptr [rbp + " + std::to_string(offset) + "]\n";
        memory(false, 0, i);
        assembly += "mov " + memName(i) + ", eax\n";
    };
    for (std::size_t i = 0; i < fn.parameterCount; ++i) {
        if (i >= regArgs) stackIncoming(i);
        else {
            if ((win64 && i >= 2) || (!win64 && i >= 4)) byte(0x44);
            byte(0x89); byte(static_cast<std::uint8_t>(0x85 | ((win64 ? winEnc[i] : sysEnc[i]) << 3)));
            word(0u - slotOffset(i));
            assembly += "mov " + memName(i) + ", " + (win64 ? winRegs[i] : sysRegs[i]) + "\n";
        }
    }

    struct Fixup { std::size_t displacement, target; };
    std::vector<Fixup> fixups;
    std::vector<std::size_t> offsets(fn.instructions.size());
    const auto eval = [&](std::size_t depth) { return fn.slotCount + depth; };
    const auto read = [&](unsigned reg, std::size_t slot, const char *name) {
        memory(true, reg, slot);
        assembly += std::string("mov ") + name + ", " + memName(slot) + "\n";
    };
    const auto write = [&](unsigned reg, std::size_t slot, const char *name) {
        memory(false, reg, slot);
        assembly += "mov " + memName(slot) + ", " + name + "\n";
    };
    const auto fail = [&](const char *reason) {
        out.bytes.clear(); out.error = reason; return out;
    };
    for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
        std::size_t stackDepth = fn.instructions[i].depthBefore;
        offsets[i] = code.size();
        assembly += ".L_vpp_" + std::to_string(i) + ":\n";
        const auto &op = fn.instructions[i];
        using Op = MachineOpcode;
        switch (op.opcode) {
            case Op::PushConstant:
                if (stackDepth >= fn.maxStackDepth) return fail("x86 eval stack overflow");
                byte(0xb8); word(static_cast<std::uint32_t>(op.immediate));
                assembly += "mov eax, " + std::to_string(op.immediate) + "\n";
                write(0, eval(stackDepth++), "eax"); break;
            case Op::LoadSlot:
                if (op.operand >= fn.slotCount || stackDepth >= fn.maxStackDepth)
                    return fail("x86 load slot ngoài frame");
                read(0, op.operand, "eax");
                write(0, eval(stackDepth++), "eax"); break;
            case Op::StoreSlot:
                if (op.operand >= fn.slotCount || !stackDepth)
                    return fail("x86 store slot ngoài frame");
                read(0, eval(stackDepth - 1), "eax");
                write(0, op.operand, "eax"); break;
            case Op::Drop:
                if (!stackDepth) return fail("x86 eval stack underflow");
                --stackDepth; break;
            // Legacy C++ oracle remains restricted; production i32 division
            // is emitted directly by the V++ backend.
            case Op::Div: case Op::Mod:
                return fail("x86 Div/Mod: dùng backend V++ native");
            case Op::Add: case Op::Sub: case Op::Mul:
            case Op::Equal: case Op::NotEqual: case Op::Less: case Op::LessEqual:
            case Op::Greater: case Op::GreaterEqual: {
                if (stackDepth < 2) return fail("x86 binary stack underflow");
                read(0, eval(stackDepth - 2), "eax");
                read(1, eval(stackDepth - 1), "ecx");
                if (op.opcode == Op::Add || op.opcode == Op::Sub) {
                    byte(op.opcode == Op::Add ? 0x01 : 0x29); byte(0xc8);
                    assembly += std::string(op.opcode == Op::Add ? "add" : "sub") + " eax, ecx\n";
                } else if (op.opcode == Op::Mul) {
                    byte(0x0f); byte(0xaf); byte(0xc1);
                    assembly += "imul eax, ecx\n";
                } else {
                    byte(0x39); byte(0xc8); assembly += "cmp eax, ecx\n";
                    const char *mnemonic = "";
                    std::uint8_t cc = 0;
                    switch (op.opcode) {
                        case Op::Equal: cc = 0x94; mnemonic = "sete"; break;
                        case Op::NotEqual: cc = 0x95; mnemonic = "setne"; break;
                        case Op::Less: cc = 0x9c; mnemonic = "setl"; break;
                        case Op::LessEqual: cc = 0x9e; mnemonic = "setle"; break;
                        case Op::Greater: cc = 0x9f; mnemonic = "setg"; break;
                        default: cc = 0x9d; mnemonic = "setge"; break;
                    }
                    byte(0x0f); byte(cc); byte(0xc0);
                    byte(0x0f); byte(0xb6); byte(0xc0);
                    assembly += std::string(mnemonic) + " al\nmovzx eax, al\n";
                }
                write(0, eval(--stackDepth - 1), "eax"); break;
            }
            case Op::BranchZero:
                if (!stackDepth || op.operand >= fn.instructions.size())
                    return fail("x86 branch không hợp lệ");
                read(0, eval(--stackDepth), "eax");
                byte(0x85); byte(0xc0);
                byte(0x0f); byte(0x84);
                fixups.push_back({code.size(), op.operand}); word(0);
                assembly += "test eax, eax\nje .L_vpp_" + std::to_string(op.operand) + "\n";
                break;
            case Op::Jump:
                if (op.operand >= fn.instructions.size()) return fail("x86 jump không hợp lệ");
                byte(0xe9); fixups.push_back({code.size(), op.operand}); word(0);
                assembly += "jmp .L_vpp_" + std::to_string(op.operand) + "\n"; break;
            case Op::Call: {
                if (stackDepth < op.operand || op.symbol.empty())
                    return fail("x86 call thiếu arguments/symbol");
                const auto extra = op.operand > regArgs ? op.operand - regArgs : 0;
                if (extra > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8) - 8)
                    return fail("x86 call stack quá lớn");
                const auto outgoing = static_cast<std::uint32_t>(
                    ((extra * 8 + (win64 ? 32 : 0) + 15) & ~std::size_t(15)));
                if (outgoing) {
                    byte(0x48); byte(0x81); byte(0xec); word(outgoing);
                    assembly += "sub rsp, " + std::to_string(outgoing) + "\n";
                }
                for (std::size_t n = regArgs; n < op.operand; ++n) {
                    // Spill stack-passed i32 arguments into 8-byte ABI slots.
                    read(0, eval(stackDepth - op.operand + n), "eax");
                    byte(0x89); byte(0x84); byte(0x24);
                    const auto offset = static_cast<std::uint32_t>(
                        (win64 ? 32 : 0) + 8 * (n - regArgs));
                    word(offset);
                    assembly += "mov dword ptr [rsp + " + std::to_string(offset) + "], eax\n";
                }
                for (std::size_t n = 0; n < std::min<std::size_t>(op.operand, regArgs); ++n) {
                    const auto slot = eval(stackDepth - op.operand + n);
                    const unsigned reg = win64 ? winEnc[n] : sysEnc[n];
                    if ((win64 && n >= 2) || (!win64 && n >= 4)) byte(0x44);
                    memory(true, reg, slot);
                    assembly += "mov " + (win64 ? winRegs[n] : sysRegs[n]) +
                                ", " + memName(slot) + "\n";
                }
                byte(0xe8);
                out.relocations.push_back({code.size(), op.symbol});
                word(0);
                assembly += std::string("call ") + (target.os == OperatingSystem::MacOS ? "_" : "") +
                            op.symbol + "\n";
                if (outgoing) {
                    byte(0x48); byte(0x81); byte(0xc4); word(outgoing);
                    assembly += "add rsp, " + std::to_string(outgoing) + "\n";
                }
                write(0, eval(stackDepth - op.operand), "eax");
                break;
            }
            case Op::Return:
                if (!stackDepth) return fail("x86 return không có giá trị");
                read(0, eval(--stackDepth), "eax");
                byte(0xc9); byte(0xc3); assembly += "leave\nret\n"; break;
            case Op::Trap:
                byte(0x0f); byte(0x0b); assembly += "ud2\n"; break;
        }
    }
    for (const auto &fix : fixups) {
        const auto delta = static_cast<std::int64_t>(offsets[fix.target]) -
                           static_cast<std::int64_t>(fix.displacement + 4);
        if (delta < std::numeric_limits<std::int32_t>::min() ||
            delta > std::numeric_limits<std::int32_t>::max())
            return fail("x86 branch vượt quá rel32");
        const auto encoded = static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
        for (unsigned n = 0; n < 4; ++n)
            code[fix.displacement + n] = static_cast<std::uint8_t>(encoded >> (8 * n));
    }
    return out;
}

} // namespace vietvm::native
