#include "vpp/native/target.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace vietvm::native {
namespace {
const std::vector<ArchitectureInfo> kArchitectures = {
    {Architecture::X86_64, "x86_64", 64, true},
    {Architecture::AArch64, "aarch64", 64, true},
    {Architecture::RiscV64, "riscv64", 64, true},
    {Architecture::ArmV7, "armv7", 32, true},
    {Architecture::I386, "i386", 32, true},
    {Architecture::PowerPc, "powerpc", 32, false},
    {Architecture::PowerPc64, "powerpc64", 64, false},
    {Architecture::PowerPc64Le, "powerpc64le", 64, true},
    {Architecture::Mips, "mips", 32, false},
    {Architecture::MipsLe, "mipsel", 32, true},
    {Architecture::Mips64, "mips64", 64, false},
    {Architecture::Mips64Le, "mips64el", 64, true},
    {Architecture::LoongArch64, "loongarch64", 64, true},
    {Architecture::S390X, "s390x", 64, false},
    {Architecture::Sparc, "sparc", 32, false},
    {Architecture::Sparc64, "sparc64", 64, false},
};

std::string canonicalArch(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::unordered_map<std::string, std::string> aliases = {
        {"amd64", "x86_64"}, {"x64", "x86_64"}, {"arm64", "aarch64"},
        {"arm64e", "aarch64"}, {"rv64", "riscv64"},
        {"arm", "armv7"}, {"armhf", "armv7"}, {"armv7l", "armv7"},
        {"x86", "i386"}, {"i486", "i386"}, {"i586", "i386"},
        {"i686", "i386"}, {"ppc", "powerpc"},
        {"ppc64", "powerpc64"}, {"ppc64le", "powerpc64le"},
        {"mips32", "mips"}, {"mips32el", "mipsel"},
        {"sparcv9", "sparc64"},
    };
    auto found = aliases.find(input);
    return found == aliases.end() ? input : found->second;
}

const char *abiName(Abi abi) {
    switch (abi) {
        case Abi::Gnu: return "gnu";
        case Abi::GnuEabiHf: return "gnueabihf";
        case Abi::Musl: return "musl";
        case Abi::MuslEabiHf: return "musleabihf";
        case Abi::Msvc: return "msvc";
        case Abi::Darwin: return "darwin";
        default: return "unknown";
    }
}
} // namespace

const std::vector<ArchitectureInfo> &registeredArchitectures() { return kArchitectures; }

std::string_view architectureName(Architecture arch) noexcept {
    for (const auto &item : kArchitectures)
        if (item.arch == arch) return item.name;
    return "unknown";
}

std::string_view objectFormatName(ObjectFormat format) noexcept {
    switch (format) {
        case ObjectFormat::Elf: return "ELF";
        case ObjectFormat::Coff: return "COFF";
        case ObjectFormat::MachO: return "Mach-O";
        default: return "unknown";
    }
}

TargetParseResult parseTarget(std::string_view triple) {
    std::string input(triple);
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto dash = input.find('-');
    if (dash == std::string::npos || dash == 0)
        return {{}, "target cần kiến trúc, hệ điều hành và ABI"};

    const std::string archName = canonicalArch(input.substr(0, dash));
    Target target;
    for (const auto &info : kArchitectures)
        if (archName == info.name) target.arch = info.arch;
    if (target.arch == Architecture::Unknown)
        return {{}, "kiến trúc CPU chưa được đăng ký: " + archName};

    std::string rest = input.substr(dash + 1);
    // Common GNU/LLVM vendor triples and their shorter aliases.
    for (const std::string vendor : {"unknown-", "pc-", "w64-", "apple-"}) {
        if (rest.compare(0, vendor.size(), vendor) == 0) {
            rest.erase(0, vendor.size());
            break;
        }
    }
    if (rest == "linux" || rest == "linux-gnu") {
        target.os = OperatingSystem::Linux;
        target.abi = Abi::Gnu;
        target.objectFormat = ObjectFormat::Elf;
    } else if (rest == "linux-gnueabihf") {
        target.os = OperatingSystem::Linux;
        target.abi = Abi::GnuEabiHf;
        target.objectFormat = ObjectFormat::Elf;
    } else if (rest == "linux-musl") {
        target.os = OperatingSystem::Linux;
        target.abi = Abi::Musl;
        target.objectFormat = ObjectFormat::Elf;
    } else if (rest == "linux-musleabihf") {
        target.os = OperatingSystem::Linux;
        target.abi = Abi::MuslEabiHf;
        target.objectFormat = ObjectFormat::Elf;
    } else if (rest == "windows-msvc") {
        target.os = OperatingSystem::Windows;
        target.abi = Abi::Msvc;
        target.objectFormat = ObjectFormat::Coff;
    } else if (rest == "windows-gnu" || rest == "mingw32") {
        target.os = OperatingSystem::Windows;
        target.abi = Abi::Gnu;
        target.objectFormat = ObjectFormat::Coff;
    } else if (rest == "darwin" || rest == "macos") {
        target.os = OperatingSystem::MacOS;
        target.abi = Abi::Darwin;
        target.objectFormat = ObjectFormat::MachO;
    } else {
        return {{}, "hệ điều hành hoặc ABI chưa hỗ trợ: " + rest};
    }

    if (target.os == OperatingSystem::MacOS &&
        target.arch != Architecture::AArch64 && target.arch != Architecture::X86_64)
        return {{}, "kiến trúc này không có target macOS"};
    if (target.os == OperatingSystem::Windows &&
        target.arch != Architecture::X86_64 && target.arch != Architecture::I386 &&
        target.arch != Architecture::AArch64)
        return {{}, "kiến trúc này không có target Windows"};
    if (target.os != OperatingSystem::Linux &&
        (target.abi == Abi::GnuEabiHf || target.abi == Abi::MuslEabiHf))
        return {{}, "ABI EABI chỉ dành cho Linux"};
    return {target, {}};
}

std::string canonicalTriple(const Target &target) {
    std::string name(architectureName(target.arch));
    if (target.os == OperatingSystem::MacOS) return name + "-apple-darwin";
    if (target.os == OperatingSystem::Windows)
        return name + "-" + (target.abi == Abi::Msvc ? "pc" : "w64") +
               "-windows-" + abiName(target.abi);
    if (target.os == OperatingSystem::Linux)
        return name + "-unknown-linux-" + abiName(target.abi);
    return name + "-unknown-unknown";
}

Target hostTarget() noexcept {
    Target target;
#if defined(__x86_64__) || defined(_M_X64)
    target.arch = Architecture::X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    target.arch = Architecture::AArch64;
#elif defined(__i386__) || defined(_M_IX86)
    target.arch = Architecture::I386;
#elif defined(__riscv) && __riscv_xlen == 64
    target.arch = Architecture::RiscV64;
#elif defined(__arm__)
    target.arch = Architecture::ArmV7;
#endif
#if defined(__APPLE__)
    target.os = OperatingSystem::MacOS;
    target.abi = Abi::Darwin;
    target.objectFormat = ObjectFormat::MachO;
#elif defined(_WIN32)
    target.os = OperatingSystem::Windows;
    target.abi = Abi::Msvc;
    target.objectFormat = ObjectFormat::Coff;
#elif defined(__linux__)
    target.os = OperatingSystem::Linux;
    target.abi = Abi::Gnu;
    target.objectFormat = ObjectFormat::Elf;
#endif
    return target;
}

bool hasExperimentalMachineJit(const Target &target) noexcept {
    return (target.arch == Architecture::AArch64 &&
            (target.os == OperatingSystem::Linux || target.os == OperatingSystem::MacOS)) ||
           (target.arch == Architecture::X86_64 &&
            (target.os == OperatingSystem::Linux || target.os == OperatingSystem::Windows));
}

} // namespace vietvm::native
