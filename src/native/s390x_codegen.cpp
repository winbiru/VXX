#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone s390x machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitS390X(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        (void)target;
        if (leaf.constant < -32768 || leaf.constant > 32767) {
            // LGFI r2, imm32 (RIL-a, C0 21) sign-extends the complete
            // 32-bit value into the ABI result register on z/Architecture.
            // Keep the shorter LHI encoding for signed i16 values.
            out.bytes = {0xc0, 0x21};
            appendWord(out.bytes, static_cast<std::uint32_t>(leaf.constant), false);
            out.assembly = "lgfi %r2, " + std::to_string(leaf.constant) + "\nbr %r14\n";
        } else {
            out.bytes = {0xa7, 0x29,
                static_cast<std::uint8_t>(static_cast<std::uint16_t>(leaf.constant) >> 8),
                static_cast<std::uint8_t>(leaf.constant)};
            out.assembly = "lhi %r2, " + std::to_string(leaf.constant) + "\nbr %r14\n";
        }
        out.bytes.push_back(0x07); out.bytes.push_back(0xfe); // BR R14
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        (void)target;
        out.bytes = {0x1a, 0x23, 0x07, 0xfe}; // AR R2,R3; BR R14
        out.assembly = "ar %r2, %r3\nbr %r14\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        out.bytes = {0x1b, 0x23, 0x07, 0xfe}; // SR R2,R3; BR R14
        out.assembly = "sr %r2, %r3\nbr %r14\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
