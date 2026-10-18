#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone armv7 machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitArmV7(const RawLeaf &leaf, const Target &target) {
    (void)target;
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        const auto bits = static_cast<std::uint32_t>(leaf.constant);
        const auto low = bits & 0xffffu;
        const auto high = bits >> 16;
        // A32 MOVW/MOVT immediate encoding splits imm16 into imm4 and imm12.
        appendWord(out.bytes, 0xe3000000u | ((low & 0xf000u) << 4) |
            (low & 0x0fffu), true);
        appendWord(out.bytes, 0xe3400000u | ((high & 0xf000u) << 4) |
            (high & 0x0fffu), true);
        appendWord(out.bytes, 0xe12fff1eu, true); // BX LR
        out.assembly = "movw r0, #" + std::to_string(low) +
            "\nmovt r0, #" + std::to_string(high) + "\nbx lr\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        appendWord(out.bytes, 0xe0800001u, true); // ADD R0,R0,R1 (A32)
        appendWord(out.bytes, 0xe12fff1eu, true); // BX LR
        out.assembly = "add r0, r0, r1\nbx lr\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0xe0400001u, true); // SUB R0,R0,R1 (A32)
        appendWord(out.bytes, 0xe12fff1eu, true);
        out.assembly = "sub r0, r0, r1\nbx lr\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
