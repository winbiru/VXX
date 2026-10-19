#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vpp/native/raw_codegen.h"

namespace vietvm::native {

// Private contract shared by independent ISA emitters. Each backend owns
// its instruction encodings and assembly mnemonics in one .cpp file.
struct BackendEmission {
    std::vector<std::uint8_t> bytes;
    std::string assembly;
    std::string error;
    std::vector<MachineRelocation> relocations;
};

inline void appendWord(std::vector<std::uint8_t> &out,
                       std::uint32_t word, bool littleEndian) {
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned shift = (littleEndian ? i : (3 - i)) * 8;
        out.push_back(static_cast<std::uint8_t>(word >> shift));
    }
}

BackendEmission emitX86_64(const RawLeaf &, const Target &);
BackendEmission emitAArch64(const RawLeaf &, const Target &);
BackendEmission emitX86_64Function(const MachineFunction &, const Target &);
BackendEmission emitAArch64Function(const MachineFunction &, const Target &);
BackendEmission emitRiscV64(const RawLeaf &, const Target &);
BackendEmission emitArmV7(const RawLeaf &, const Target &);
BackendEmission emitI386(const RawLeaf &, const Target &);
BackendEmission emitPowerPc(const RawLeaf &, const Target &);
BackendEmission emitPowerPc64(const RawLeaf &, const Target &);
BackendEmission emitPowerPc64Le(const RawLeaf &, const Target &);
BackendEmission emitMips(const RawLeaf &, const Target &);
BackendEmission emitMipsLe(const RawLeaf &, const Target &);
BackendEmission emitMips64(const RawLeaf &, const Target &);
BackendEmission emitMips64Le(const RawLeaf &, const Target &);
BackendEmission emitLoongArch64(const RawLeaf &, const Target &);
BackendEmission emitS390X(const RawLeaf &, const Target &);
BackendEmission emitSparc(const RawLeaf &, const Target &);
BackendEmission emitSparc64(const RawLeaf &, const Target &);

} // namespace vietvm::native
