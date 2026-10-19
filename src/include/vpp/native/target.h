#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vietvm::native {

// CPU family and endianness are part of the target identity. In particular,
// ppc64le/mipsel cannot share an encoder with their big-endian variants.
enum class Architecture {
    Unknown, X86_64, AArch64, RiscV64, ArmV7, I386,
    PowerPc, PowerPc64, PowerPc64Le,
    Mips, MipsLe, Mips64, Mips64Le,
    LoongArch64, S390X, Sparc, Sparc64
};

enum class OperatingSystem { Unknown, Linux, Windows, MacOS };
enum class Abi { Unknown, Gnu, GnuEabiHf, Musl, MuslEabiHf, Msvc, Darwin };
enum class ObjectFormat { Unknown, Elf, Coff, MachO };

struct Target {
    Architecture arch = Architecture::Unknown;
    OperatingSystem os = OperatingSystem::Unknown;
    Abi abi = Abi::Unknown;
    ObjectFormat objectFormat = ObjectFormat::Unknown;
};

struct ArchitectureInfo {
    Architecture arch;
    const char *name;
    unsigned bits;
    bool littleEndian;
};

struct TargetParseResult {
    std::optional<Target> target;
    std::string error;
};

const std::vector<ArchitectureInfo> &registeredArchitectures();
std::string_view architectureName(Architecture arch) noexcept;
std::string_view objectFormatName(ObjectFormat format) noexcept;
TargetParseResult parseTarget(std::string_view triple);
std::string canonicalTriple(const Target &target);
Target hostTarget() noexcept;

// Experimental JIT host support. The separate, opt-in self-host AOT path
// also emits host executables for the supported i32 subset on x86_64/AArch64.
bool hasExperimentalMachineJit(const Target &target) noexcept;

} // namespace vietvm::native
