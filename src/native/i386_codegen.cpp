#include "raw_backend.h"

#include <cstdint>
#include <string>

namespace vietvm::native {

// Standalone i386 machine-code encoder; one leaf function body per call.
// The JITs in aarch64_jit.cpp and x64_jit.cpp own executable memory separately.
BackendEmission emitI386(const RawLeaf &leaf, const Target &target) {
    BackendEmission out;
    if (leaf.kind == RawLeafKind::ReturnConstantI32) {
        (void)target;
        out.bytes.push_back(0xb8);
        appendWord(out.bytes, static_cast<std::uint32_t>(leaf.constant), true);
        out.bytes.push_back(0xc3);
        out.assembly = "mov eax, " + std::to_string(leaf.constant) + "\nret\n";
    } else if (leaf.kind == RawLeafKind::AddI32Arguments) {
        (void)target;
        out.bytes = {0x8b, 0x44, 0x24, 0x04, 0x03, 0x44, 0x24, 0x08, 0xc3};
        // cdecl: [esp+4]=a; [esp+8]=b, result EAX.
        out.assembly = "mov eax, [esp+4]\nadd eax, [esp+8]\nret\n";
    } else if (leaf.kind == RawLeafKind::SubI32Arguments) {
        (void)target;
        out.bytes = {0x8b, 0x44, 0x24, 0x04, 0x2b, 0x44, 0x24, 0x08, 0xc3};
        out.assembly = "mov eax, [esp+4]\nsub eax, [esp+8]\nret\n";
    } else {
        out.error = "unsupported native leaf instruction";
    }
    return out;
}

} // namespace vietvm::native
