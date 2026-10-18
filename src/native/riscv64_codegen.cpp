#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone riscv64 machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitRiscV64(const RawLeaf &leaf, const Target &target) {
    (void)target;
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        const auto value = static_cast<std::int64_t>(leaf.constant);
        // Round to nearest 4K with a signed 12-bit ADDIW remainder.
        const auto adjusted = value + 2048;
        const auto high = adjusted >= 0 ? adjusted / 4096 : (adjusted - 4095) / 4096;
        const auto low = value - high * 4096;
        appendWord(out.bytes, ((static_cast<std::uint32_t>(high) & 0xfffffu) << 12) |
            0x537u, true); // LUI A0,hi20
        appendWord(out.bytes, ((static_cast<std::uint32_t>(low) & 0xfffu) << 20) |
            0x5051bu, true); // ADDIW A0,A0,lo12
        appendWord(out.bytes, 0x00008067u, true); // JALR zero,ra,0
        out.assembly = "lui a0, " + std::to_string(high) +
            "\naddiw a0, a0, " + std::to_string(low) + "\njalr zero, ra, 0\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        appendWord(out.bytes, 0x00b5053bu, true); // ADDW A0,A0,A1
        appendWord(out.bytes, 0x00008067u, true); // RET
        out.assembly = "addw a0, a0, a1\njalr zero, ra, 0\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0x40b5053bu, true); // SUBW A0,A0,A1
        appendWord(out.bytes, 0x00008067u, true);
        out.assembly = "subw a0, a0, a1\njalr zero, ra, 0\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
