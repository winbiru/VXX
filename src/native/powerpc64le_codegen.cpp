#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone powerpc64le machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitPowerPc64Le(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        (void)target;
        const auto bits = static_cast<std::uint32_t>(leaf.constant);
        appendWord(out.bytes, 0x3c600000u | (bits >> 16), true); // LIS R3,hi16
        appendWord(out.bytes, 0x60630000u | (bits & 0xffffu), true); // ORI R3,R3,lo16
        appendWord(out.bytes, 0x4e800020u, true); // BLR
        out.assembly = "lis 3, " + std::to_string(bits >> 16) +
            "\nori 3, 3, " + std::to_string(bits & 0xffffu) + "\nblr\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        (void)target;
        appendWord(out.bytes, 0x7c632214u, true); // ADD R3,R3,R4
        appendWord(out.bytes, 0x4e800020u, true); // BLR
        out.assembly = "add 3, 3, 4\nblr\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0x7c641850u, true); // SUBF R3,R4,R3: R3-R4
        appendWord(out.bytes, 0x4e800020u, true);
        out.assembly = "subf 3, 4, 3\nblr\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
