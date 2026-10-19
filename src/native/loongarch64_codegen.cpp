#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone loongarch64 machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitLoongArch64(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        (void)target;
        const auto bits = static_cast<std::uint32_t>(leaf.constant);
        const auto upper = (bits >> 12) & 0xfffffu;
        const auto lower = bits & 0xfffu;
        appendWord(out.bytes, 0x14000004u | (upper << 5), true); // LU12I.W A0,hi20
        appendWord(out.bytes, 0x03800084u | (lower << 10), true); // ORI A0,A0,lo12
        appendWord(out.bytes, 0x4c000020u, true); // JIRL R0,RA,0
        out.assembly = "lu12i.w $a0, " + std::to_string(upper) +
            "\nori $a0, $a0, " + std::to_string(lower) + "\njirl $zero, $ra, 0\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        (void)target;
        appendWord(out.bytes, 0x00101484u, true); // ADD.W A0,A0,A1
        appendWord(out.bytes, 0x4c000020u, true);
        out.assembly = "add.w $a0, $a0, $a1\njirl $zero, $ra, 0\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        appendWord(out.bytes, 0x00111484u, true); // SUB.W A0,A0,A1
        appendWord(out.bytes, 0x4c000020u, true);
        out.assembly = "sub.w $a0, $a0, $a1\njirl $zero, $ra, 0\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
