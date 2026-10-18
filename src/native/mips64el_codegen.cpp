#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone mips64el machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitMips64Le(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        (void)target;
        const auto bits = static_cast<std::uint32_t>(leaf.constant);
        appendWord(out.bytes, 0x3c020000u | (bits >> 16), true); // LUI V0,hi16
        appendWord(out.bytes, 0x34420000u | (bits & 0xffffu), true); // ORI V0,V0,lo16
        appendWord(out.bytes, 0x03e00008u, true); // JR RA
        appendWord(out.bytes, 0u, true); // mandatory delay slot
        out.assembly = "lui $v0, " + std::to_string(bits >> 16) +
            "\nori $v0, $v0, " + std::to_string(bits & 0xffffu) +
            "\njr $ra\nnop\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        (void)target;
        appendWord(out.bytes, 0x00851021u, true); // ADDU V0,A0,A1 (wrap i32)
        appendWord(out.bytes, 0x03e00008u, true); // JR RA
        appendWord(out.bytes, 0u, true); // delay slot
        out.assembly = "addu $v0, $a0, $a1\njr $ra\nnop\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0x00851023u, true); // SUBU V0,A0,A1
        appendWord(out.bytes, 0x03e00008u, true);
        appendWord(out.bytes, 0u, true);
        out.assembly = "subu $v0, $a0, $a1\njr $ra\nnop\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
