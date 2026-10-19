#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone sparc machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitSparc(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        (void)target;
        const auto bits = static_cast<std::uint32_t>(leaf.constant);
        const auto upper = bits >> 10;
        const auto lower = bits & 1023u;
        appendWord(out.bytes, 0x11000000u | upper, false); // SETHI %hi, %o0
        appendWord(out.bytes, 0x90122000u | lower, false); // OR %o0, %lo, %o0
        appendWord(out.bytes, 0x81c3e008u, false); // RETL
        appendWord(out.bytes, 0x01000000u, false); // delay slot NOP
        out.assembly = "sethi " + std::to_string(upper) + ", %o0\nor %o0, " +
            std::to_string(lower) + ", %o0\nretl\nnop\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        (void)target;
        appendWord(out.bytes, 0x90020009u, false); // ADD %o0,%o1,%o0
        appendWord(out.bytes, 0x81c3e008u, false); // RETL
        appendWord(out.bytes, 0x01000000u, false); // NOP delay slot
        out.assembly = "add %o0, %o1, %o0\nretl\nnop\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0x90220009u, false); // SUB %o0,%o1,%o0
        appendWord(out.bytes, 0x81c3e008u, false);
        appendWord(out.bytes, 0x01000000u, false);
        out.assembly = "sub %o0, %o1, %o0\nretl\nnop\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
