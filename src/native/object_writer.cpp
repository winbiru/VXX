#include "vpp/native/raw_codegen.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vietvm::native {
namespace {

// Little- and big-endian object writers have the same structure, but ELF
// formats vary by word size and CPU. All offsets are calculated from the
// current buffer position; a mismatch is caught by the object tests.
struct Writer {
    std::vector<std::uint8_t> bytes;
    bool little = true;

    explicit Writer(bool littleEndian = true) : little(littleEndian) {}
    std::size_t pos() const { return bytes.size(); }
    void u8(std::uint64_t x) { bytes.push_back(static_cast<std::uint8_t>(x)); }
    void number(std::uint64_t x, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            u8(x >> (8 * (little ? i : n - 1 - i)));
    }
    void u16(std::uint64_t x) { number(x, 2); }
    void u32(std::uint64_t x) { number(x, 4); }
    void u64(std::uint64_t x) { number(x, 8); }
    void append(const std::vector<std::uint8_t> &src) {
        bytes.insert(bytes.end(), src.begin(), src.end());
    }
    void append(std::string_view src) {
        bytes.insert(bytes.end(), src.begin(), src.end());
    }
    void fixed(std::string_view str, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            u8(i < str.size() ? static_cast<std::uint8_t>(str[i]) : 0);
    }
    void align(unsigned boundary) {
        while (bytes.size() % boundary) u8(0);
    }
    void patch(std::size_t offset, std::uint64_t x, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            bytes.at(offset + i) = static_cast<std::uint8_t>(
                x >> (8 * (little ? i : n - 1 - i)));
    }
};

bool safeName(std::string_view symbol) {
    if (symbol.empty() ||
        !(std::isalpha(static_cast<unsigned char>(symbol[0])) || symbol[0] == '_'))
        return false;
    for (unsigned char ch : symbol)
        if (!std::isalnum(ch) && ch != '_') return false;
    return true;
}

std::uint16_t elfMachine(Architecture arch) {
    switch (arch) {
        case Architecture::X86_64: return 62;
        case Architecture::AArch64: return 183;
        case Architecture::RiscV64: return 243;
        case Architecture::ArmV7: return 40;
        case Architecture::I386: return 3;
        case Architecture::PowerPc: return 20;
        case Architecture::PowerPc64:
        case Architecture::PowerPc64Le: return 21;
        case Architecture::Mips: case Architecture::MipsLe:
        case Architecture::Mips64: case Architecture::Mips64Le: return 8;
        case Architecture::LoongArch64: return 258;
        case Architecture::S390X: return 22;
        case Architecture::Sparc: return 2;
        case Architecture::Sparc64: return 43;
        default: return 0;
    }
}

bool is32(Architecture arch) {
    switch (arch) {
        case Architecture::ArmV7: case Architecture::I386:
        case Architecture::PowerPc: case Architecture::Mips:
        case Architecture::MipsLe: case Architecture::Sparc: return true;
        default: return false;
    }
}

bool isLittle(Architecture arch) {
    switch (arch) {
        case Architecture::PowerPc: case Architecture::PowerPc64:
        case Architecture::Mips: case Architecture::Mips64:
        case Architecture::S390X: case Architecture::Sparc:
        case Architecture::Sparc64: return false;
        default: return true;
    }
}

unsigned alignment(Architecture arch) {
    return (arch == Architecture::X86_64 || arch == Architecture::I386) ? 1 : 4;
}

RawObjectResult elfObject(const Target &target,
                          const std::vector<std::uint8_t> &code,
                          std::string_view symbol,
                          const std::vector<MachineRelocation> &relocs) {
    const bool small = is32(target.arch);
    const bool little = isLittle(target.arch);
    // PowerPC64 big-endian Linux uses ELFv1: its exported function symbol
    // denotes a three-word descriptor in .opd, not the instruction address.
    const bool ppc64ElfV1 = target.arch == Architecture::PowerPc64;
    const bool textRelocs = !relocs.empty();
    if (textRelocs && target.arch != Architecture::X86_64 &&
        target.arch != Architecture::AArch64)
        return {{}, "ELF external CALL chỉ hỗ trợ x86_64/AArch64"};
    const std::uint16_t machine = elfMachine(target.arch);
    if (!machine) return {{}, "ELF machine chưa được đăng ký"};
    const unsigned word = small ? 4 : 8;
    const unsigned headerSize = small ? 52 : 64;
    const unsigned sectionSize = small ? 40 : 64;
    const unsigned symbolSize = small ? 16 : 24;
    Writer w(little);
    w.u8(0x7f); w.append("ELF");
    w.u8(small ? 1 : 2); w.u8(little ? 1 : 2);
    w.u8(1); w.u8(0); w.u8(0);
    while (w.pos() < 16) w.u8(0);
    w.u16(1); // ET_REL
    w.u16(machine);
    w.u32(1); // EV_CURRENT
    w.number(0, word); // e_entry
    w.number(0, word); // e_phoff
    const std::size_t shoffPatch = w.pos();
    w.number(0, word); // e_shoff, patched after sections
    std::uint32_t flags = 0;
    if (target.arch == Architecture::ArmV7) {
        flags = 0x05000000u; // EF_ARM_EABI_VER5
        if (target.abi == Abi::GnuEabiHf || target.abi == Abi::MuslEabiHf)
            flags |= 0x400u; // EF_ARM_ABI_FLOAT_HARD
    }
    // Linux RISC-V/LoongArch toolchains normally use LP64D (double-float)
    // even when a leaf happens to pass only integer values. ELF linkers
    // check these ABI flags against the other translation units.
    if (target.arch == Architecture::RiscV64) flags = 0x4u; // EF_RISCV_FLOAT_ABI_DOUBLE
    if (target.arch == Architecture::LoongArch64) flags = 0x3u; // EF_LARCH_ABI_DOUBLE_FLOAT
    if (ppc64ElfV1) flags = 1; // EF_PPC64_ABI: ELFv1
    if (target.arch == Architecture::PowerPc64Le) flags = 2; // ELFv2 ABI
    w.u32(flags);
    w.u16(headerSize);
    w.u16(0); w.u16(0); // no program headers
    w.u16(sectionSize);
    w.u16(ppc64ElfV1 ? 7 : (textRelocs ? 6 : 5));
    w.u16(ppc64ElfV1 ? 6 : (textRelocs ? 5 : 4)); // section count, shstr index

    w.align(alignment(target.arch));
    const auto textOff = w.pos();
    w.append(code);
    w.align(word);
    std::size_t opdOff = 0, relaOff = 0;
    if (ppc64ElfV1) {
        opdOff = w.pos();
        w.u64(0); // function entry: R_PPC64_ADDR64 against .text
        w.u64(0); // TOC: unused by these self-contained integer leaves
        w.u64(0); // environment pointer
        relaOff = w.pos();
        w.u64(0); // r_offset in .opd
        w.u64((std::uint64_t{1} << 32) | 38); // local .text section, R_PPC64_ADDR64
        w.u64(0); // r_addend
    }
    std::size_t textRelaOff = 0;
    if (textRelocs) {
        textRelaOff = w.pos();
        for (std::size_t i = 0; i < relocs.size(); ++i) {
            w.u64(relocs[i].offset);
            w.u64((std::uint64_t{2 + i} << 32) |
                  (target.arch == Architecture::X86_64 ? 4u : 283u));
            w.u64(target.arch == Architecture::X86_64 ? std::uint64_t(-4) : 0);
        }
    }
    const auto symOff = w.pos();
    for (unsigned i = 0; i < symbolSize; ++i) w.u8(0); // STN_UNDEF
    if (ppc64ElfV1) {
        w.u32(0); w.u8(3); w.u8(0); w.u16(1); // local STT_SECTION: .text
        w.u64(0); w.u64(0);
    }
    if (small) {
        w.u32(1); w.u32(0); w.u32(code.size());
        w.u8(0x12); w.u8(0); w.u16(1); // STB_GLOBAL|STT_FUNC, .text
    } else {
        w.u32(1); w.u8(0x12); w.u8(0); w.u16(ppc64ElfV1 ? 2 : 1);
        w.u64(0); w.u64(ppc64ElfV1 ? 24 : code.size());
    }
    std::vector<std::uint32_t> externalNames;
    std::string strings(1, '\0');
    strings.append(symbol); strings.push_back('\0');
    for (const auto &reloc : relocs) {
        externalNames.push_back(static_cast<std::uint32_t>(strings.size()));
        strings.append(reloc.symbol); strings.push_back('\0');
    }
    for (auto offset : externalNames) {
        w.u32(offset); w.u8(0x12); w.u8(0); w.u16(0); // undefined STB_GLOBAL FUNC
        w.u64(0); w.u64(0);
    }
    const auto strOff = w.pos();
    w.append(std::string_view(strings));
    const auto strLen = w.pos() - strOff;
    const auto shstrOff = w.pos();
    std::string names(1, '\0');
    auto addSectionName = [&](std::string_view name) {
        const auto offset = static_cast<std::uint32_t>(names.size());
        names.append(name);
        names.push_back('\0');
        return offset;
    };
    const auto textName = addSectionName(".text");
    const auto opdName = ppc64ElfV1 ? addSectionName(".opd") : 0;
    const auto relaName = ppc64ElfV1 ? addSectionName(".rela.opd") : 0;
    const auto textRelaName = textRelocs ? addSectionName(".rela.text") : 0;
    const auto symName = addSectionName(".symtab");
    const auto strName = addSectionName(".strtab");
    const auto shstrName = addSectionName(".shstrtab");
    w.append(std::string_view(names));
    w.align(word);
    const auto shoff = w.pos();
    w.patch(shoffPatch, shoff, word);

    auto section = [&](std::uint32_t name, std::uint32_t type, std::uint64_t sectionFlags,
                       std::uint64_t offset, std::uint64_t length, std::uint32_t link,
                       std::uint32_t info, std::uint64_t addralign,
                       std::uint64_t entsize) {
        w.u32(name); w.u32(type);
        w.number(sectionFlags, word); w.number(0, word);
        w.number(offset, word); w.number(length, word);
        w.u32(link); w.u32(info);
        w.number(addralign, word); w.number(entsize, word);
    };
    section(0, 0, 0, 0, 0, 0, 0, 0, 0);
    section(textName, 1, 6, textOff, code.size(), 0, 0,
            alignment(target.arch), 0);
    if (ppc64ElfV1) {
        section(opdName, 1, 3, opdOff, 24, 0, 0, 8, 0);
        section(relaName, 4, 0, relaOff, 24, 4, 2, 8, 24);
    }
    if (textRelocs)
        section(textRelaName, 4, 0, textRelaOff, relocs.size() * 24,
                3, 1, 8, 24);
    section(symName, 2, 0, symOff, (ppc64ElfV1 ? 3 : 2 + relocs.size()) * symbolSize,
            ppc64ElfV1 ? 5 : (textRelocs ? 4 : 3),
            ppc64ElfV1 ? 2 : 1, word, symbolSize);
    section(strName, 3, 0, strOff, strLen, 0, 0, 1, 0);
    section(shstrName, 3, 0, shstrOff, names.size(), 0, 0, 1, 0);
    return {std::move(w.bytes), {}};
}

RawObjectResult machoObject(const Target &target,
                            const std::vector<std::uint8_t> &code,
                            std::string_view symbol,
                            const std::vector<MachineRelocation> &relocs) {
    std::uint32_t cpu = 0, subtype = 0;
    if (target.arch == Architecture::AArch64) { cpu = 0x0100000c; subtype = 0; }
    if (target.arch == Architecture::X86_64) { cpu = 0x01000007; subtype = 3; }
    if (!cpu) return {{}, "Mach-O chỉ hỗ trợ arm64 và x86_64"};

    constexpr unsigned headerSize = 32, commandsSize = 152 + 24 + 24;
    const auto textOff = headerSize + commandsSize;
    const auto relocOff = (textOff + code.size() + 3) & ~std::size_t(3);
    const auto symOff = (relocOff + relocs.size() * 8 + 7) & ~std::size_t(7);
    const std::string exported = "_" + std::string(symbol);
    const auto strOff = symOff + 16 * (1 + relocs.size());
    std::string names(1, '\0');
    names.append(exported); names.push_back('\0');
    std::vector<std::uint32_t> nameOffsets;
    for (const auto &reloc : relocs) {
        nameOffsets.push_back(static_cast<std::uint32_t>(names.size()));
        names.push_back('_'); names.append(reloc.symbol); names.push_back('\0');
    }
    Writer w;
    w.u32(0xfeedfacfu); w.u32(cpu); w.u32(subtype);
    w.u32(1); // MH_OBJECT
    w.u32(3); w.u32(commandsSize); w.u32(0x2000); w.u32(0);
    w.u32(0x19); w.u32(152); // LC_SEGMENT_64 with one section
    w.fixed("__TEXT", 16);
    w.u64(0); w.u64(code.size()); // vmaddr, vmsize
    w.u64(textOff); w.u64(code.size()); // fileoff, filesize
    w.u32(7); w.u32(5); // maxprot, initprot
    w.u32(1); w.u32(0); // nsects, flags
    w.fixed("__text", 16); w.fixed("__TEXT", 16);
    w.u64(0); w.u64(code.size()); // section addr, size
    w.u32(textOff); w.u32(target.arch == Architecture::AArch64 ? 2 : 0);
    w.u32(relocs.empty() ? 0 : relocOff); w.u32(relocs.size());
    w.u32(0x80000400u); // regular, pure instructions, some instructions
    w.u32(0); w.u32(0); w.u32(0);
    w.u32(2); w.u32(24); // LC_SYMTAB
    w.u32(symOff); w.u32(1 + relocs.size()); w.u32(strOff); w.u32(names.size());
    w.u32(0x32); w.u32(24); // LC_BUILD_VERSION
    w.u32(1); // PLATFORM_MACOS
    w.u32(target.arch == Architecture::AArch64 ? 0x000b0000u : 0x000a0f00u);
    w.u32(0); // SDK is unspecified
    w.u32(0); // zero build tools
    if (w.pos() != textOff) return {{}, "Mach-O header size inconsistency"};
    w.append(code);
    w.align(4);
    for (std::size_t i = 0; i < relocs.size(); ++i) {
        w.u32(relocs[i].offset);
        // ARM64_RELOC_BRANCH26 / X86_64_RELOC_BRANCH: extern, PC-relative, 32 bits.
        w.u32(static_cast<std::uint32_t>(i + 1) | (1u << 24) |
              (2u << 25) | (1u << 27) | (2u << 28));
    }
    w.align(8);
    if (w.pos() != symOff) return {{}, "Mach-O symbol alignment inconsistency"};
    w.u32(1); w.u8(0x0f); w.u8(1); w.u16(0); w.u64(0); // nlist_64
    for (auto offset : nameOffsets) {
        w.u32(offset); w.u8(0x01); w.u8(0); w.u16(0); w.u64(0);
    }
    w.append(std::string_view(names));
    return {std::move(w.bytes), {}};
}

RawObjectResult coffObject(const Target &target,
                           const std::vector<std::uint8_t> &code,
                           std::string_view symbol,
                           const std::vector<MachineRelocation> &relocs) {
    std::uint16_t machine = 0;
    if (target.arch == Architecture::X86_64) machine = 0x8664;
    if (target.arch == Architecture::I386) machine = 0x014c;
    if (!machine) return {{}, "COFF chỉ hỗ trợ i386 và x86_64"};
    if (!relocs.empty() && target.arch != Architecture::X86_64)
        return {{}, "COFF external CALL chỉ hỗ trợ x86_64"};
    const std::string exported =
        (target.arch == Architecture::I386 ? "_" : "") + std::string(symbol);
    constexpr unsigned headerSize = 20, sectionSize = 40;
    const auto textOff = headerSize + sectionSize;
    const auto relocOff = textOff + code.size();
    const auto symOff = (relocOff + relocs.size() * 10 + 3) & ~std::size_t(3);
    std::string strings;
    auto encodeName = [&](Writer &writer, const std::string &name) {
        if (name.size() <= 8) writer.fixed(name, 8);
        else {
            writer.u32(0); writer.u32(static_cast<std::uint32_t>(4 + strings.size()));
            strings.append(name); strings.push_back('\0');
        }
    };
    Writer w;
    w.u16(machine); w.u16(1); w.u32(0); // machine, sections, timestamp
    w.u32(symOff); w.u32(1 + relocs.size()); // exported and undefined symbols
    w.u16(0); w.u16(0); // optional header, characteristics
    w.fixed(".text", 8);
    w.u32(0); w.u32(0); // virtual size and RVA
    w.u32(code.size()); w.u32(textOff);
    w.u32(relocs.empty() ? 0 : relocOff); w.u32(0); // relocations, line numbers
    w.u16(relocs.size()); w.u16(0);
    w.u32(0x60000020u); // code, execute, read
    w.append(code);
    for (std::size_t i = 0; i < relocs.size(); ++i) {
        w.u32(relocs[i].offset);
        w.u32(i + 1); // undefined callee at COFF symbol-table index i + 1
        w.u16(0x0004); // IMAGE_REL_AMD64_REL32
    }
    w.align(4);
    if (w.pos() != symOff) return {{}, "COFF symbol alignment inconsistency"};
    encodeName(w, exported);
    w.u32(0); w.u16(1); w.u16(0x20); w.u8(2); w.u8(0);
    for (const auto &reloc : relocs) {
        encodeName(w, reloc.symbol);
        w.u32(0); w.u16(0); w.u16(0x20); w.u8(2); w.u8(0);
    }
    w.u32(strings.size() + 4);
    w.append(std::string_view(strings));
    return {std::move(w.bytes), {}};
}

} // namespace

RawObjectResult emitRawObject(const Target &target, const RawLeaf &leaf,
                              std::string_view symbol) {
    const auto body = emitRawLeaf(target, leaf);
    if (!body) return {{}, body.error};
    return emitRawObjectBytes(target, body.bytes, symbol);
}

RawObjectResult emitRawObjectBytes(const Target &target,
                                  const std::vector<std::uint8_t> &code,
                                  std::string_view symbol) {
    return emitRawObjectBytes(target, code, symbol, {});
}

RawObjectResult emitRawObjectBytes(const Target &target,
                                  const std::vector<std::uint8_t> &code,
                                  std::string_view symbol,
                                  const std::vector<MachineRelocation> &relocs) {
    if (!safeName(symbol)) return {{}, "tên symbol native không hợp lệ"};
    if (code.empty()) return {{}, "không có mã máy để xuất object"};
    for (const auto &reloc : relocs)
        if (!safeName(reloc.symbol) || reloc.offset > code.size() ||
            code.size() - reloc.offset < 4)
            return {{}, "native object relocation offset/symbol không hợp lệ"};
    switch (target.os) {
        case OperatingSystem::Linux:
            return elfObject(target, code, symbol, relocs);
        case OperatingSystem::MacOS:
            return machoObject(target, code, symbol, relocs);
        case OperatingSystem::Windows:
            return coffObject(target, code, symbol, relocs);
        default: return {{}, "hệ điều hành không có object writer"};
    }
}

} // namespace vietvm::native
