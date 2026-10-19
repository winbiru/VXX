#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "vpp/native/raw_codegen.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint64_t field(const Bytes &bytes, std::size_t offset, unsigned width,
                    bool little = true) {
    if (offset > bytes.size() || width > bytes.size() - offset || width > 8)
        throw std::out_of_range("invalid object header offset");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        const auto index = little ? width - 1 - i : i;
        value = (value << 8) | bytes[offset + index];
    }
    return value;
}

std::string cstring(const Bytes &bytes, std::size_t start) {
    std::string value;
    while (start < bytes.size() && bytes[start])
        value.push_back(static_cast<char>(bytes[start++]));
    if (start == bytes.size()) throw std::out_of_range("unterminated object string");
    return value;
}

bool codeAt(const Bytes &bytes, std::size_t start, const Bytes &expected) {
    if (start > bytes.size() || expected.size() > bytes.size() - start)
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (bytes[start + i] != expected[i]) return false;
    return true;
}

struct Tester {
    unsigned failures = 0;
    void check(bool pass, const std::string &reason) {
        if (!pass) { std::cerr << "FAIL: " << reason << '\n'; ++failures; }
    }
};

void checkElf(Tester &test, const Bytes &obj, const Bytes &code,
              std::string_view symbol, bool small, bool little,
              std::uint16_t machine, std::uint32_t expectedFlags = 0,
              bool ppc64ElfV1 = false) {
    test.check(obj.size() >= 64 && field(obj, 0, 4, false) == 0x7f454c46,
               "ELF magic");
    test.check(obj.at(4) == (small ? 1 : 2) && obj.at(5) == (little ? 1 : 2),
               "ELF class/endianness");
    test.check(field(obj, 16, 2, little) == 1 &&
               field(obj, 18, 2, little) == machine, "ELF relocatable/machine");
    test.check(field(obj, small ? 36 : 48, 4, little) == expectedFlags,
               "ELF target ABI flags");

    const auto word = small ? 4u : 8u;
    const auto shoff = field(obj, small ? 32 : 40, word, little);
    const auto shsize = field(obj, small ? 46 : 58, 2, little);
    test.check(field(obj, small ? 48 : 60, 2, little) == (ppc64ElfV1 ? 7 : 5) &&
               field(obj, small ? 50 : 62, 2, little) == (ppc64ElfV1 ? 6 : 4),
               "ELF section count and shstrtab index");
    test.check(shsize == (small ? 40u : 64u), "ELF section-header size");
    const auto sec = [&](unsigned index) { return shoff + index * shsize; };
    const auto sectionOffset = [&](unsigned index) {
        return field(obj, sec(index) + (small ? 16 : 24), word, little);
    };
    const auto sectionLength = [&](unsigned index) {
        return field(obj, sec(index) + (small ? 20 : 32), word, little);
    };
    const unsigned symSection = ppc64ElfV1 ? 4 : 2;
    const unsigned strSection = ppc64ElfV1 ? 5 : 3;
    const unsigned shstrSection = ppc64ElfV1 ? 6 : 4;
    test.check(field(obj, sec(1) + 4, 4, little) == 1 &&
               field(obj, sec(symSection) + 4, 4, little) == 2 &&
               field(obj, sec(strSection) + 4, 4, little) == 3,
               "ELF .text/.symtab/.strtab section types");
    test.check(sectionLength(1) == code.size() &&
               codeAt(obj, sectionOffset(1), code), "ELF real machine code in .text");
    const auto symSize = small ? 16u : 24u;
    test.check(sectionLength(symSection) == (ppc64ElfV1 ? 3 : 2) * symSize &&
               field(obj, sec(symSection) + (small ? 24 : 40), 4, little) == strSection &&
               field(obj, sec(symSection) + (small ? 28 : 44), 4, little) ==
                   (ppc64ElfV1 ? 2 : 1),
               "ELF exported symbol table link and local count");
    const auto sym = sectionOffset(symSection) + (ppc64ElfV1 ? 2 : 1) * symSize;
    test.check(field(obj, sym, 4, little) == 1 &&
               obj.at(sym + (small ? 12 : 4)) == 0x12 &&
               field(obj, sym + (small ? 14 : 6), 2, little) ==
                   (ppc64ElfV1 ? 2 : 1),
               "ELF global function symbol in its ABI section");
    test.check(cstring(obj, sectionOffset(strSection) + 1) == symbol,
               "ELF exported function name");
    test.check(sectionLength(strSection) == symbol.size() + 2 &&
               cstring(obj, sectionOffset(shstrSection) + 1) == ".text",
               "ELF section string tables");
    if (ppc64ElfV1) {
        test.check(field(obj, 48, 4, false) == 1, "PowerPC64 ELFv1 ABI flag");
        test.check(field(obj, sec(2) + 4, 4, false) == 1 &&
                   field(obj, sec(2) + 8, 8, false) == 3 &&
                   sectionLength(2) == 24 &&
                   cstring(obj, sectionOffset(shstrSection) + 7) == ".opd",
                   "PowerPC64 ELFv1 .opd descriptor section");
        test.check(field(obj, sec(3) + 4, 4, false) == 4 &&
                   field(obj, sec(3) + 40, 4, false) == 4 &&
                   field(obj, sec(3) + 44, 4, false) == 2 &&
                   sectionLength(3) == 48,
                   "PowerPC64 .rela.opd metadata");
        test.check(field(obj, sectionOffset(3), 8, false) == 0 &&
                   field(obj, sectionOffset(3) + 8, 8, false) ==
                       ((std::uint64_t{1} << 32) | 38) &&
                   field(obj, sectionOffset(3) + 16, 8, false) == 0 &&
                   obj.at(sectionOffset(symSection) + symSize + 4) == 3,
                   "PowerPC64 descriptor relocates to local .text section");
        test.check(field(obj, sectionOffset(3) + 24, 8, false) == 8 &&
                   field(obj, sectionOffset(3) + 32, 8, false) == 51 &&
                   field(obj, sectionOffset(3) + 40, 8, false) == 0,
                   "PowerPC64 descriptor has R_PPC64_TOC at offset 8");
    }
}

void checkMacho(Tester &test, const Bytes &obj, const Bytes &code,
                std::string_view symbol, std::uint32_t cpu) {
    test.check(field(obj, 0, 4) == 0xfeedfacf && field(obj, 4, 4) == cpu,
               "Mach-O 64-bit header/CPU");
    test.check(field(obj, 12, 4) == 1 && field(obj, 16, 4) == 3 &&
               field(obj, 20, 4) == 200, "Mach-O object/load-command sizes");
    test.check(field(obj, 32, 4) == 0x19 && field(obj, 36, 4) == 152 &&
               field(obj, 184, 4) == 2 && field(obj, 188, 4) == 24 &&
               field(obj, 208, 4) == 0x32 && field(obj, 212, 4) == 24,
               "Mach-O segment/symtab/build-version commands");
    const auto offset = field(obj, 32 + 72 + 48, 4); // section_64.offset
    test.check(offset == 232 && codeAt(obj, offset, code),
               "Mach-O __text contains native bytes");
    const auto symbolOffset = field(obj, 192, 4);
    const auto stringOffset = field(obj, 200, 4);
    test.check(field(obj, 196, 4) == 1 && field(obj, symbolOffset, 4) == 1 &&
               obj.at(symbolOffset + 4) == 0x0f &&
               cstring(obj, stringOffset + 1) == "_" + std::string(symbol),
               "Mach-O global symbol and Darwin prefix");
}

void checkCoff(Tester &test, const Bytes &obj, const Bytes &code,
               std::string_view symbol, std::uint16_t cpu, bool i386) {
    test.check(field(obj, 0, 2) == cpu && field(obj, 2, 2) == 1,
               "COFF header/CPU");
    const auto codeSize = field(obj, 20 + 16, 4);
    const auto codeOffset = field(obj, 20 + 20, 4);
    test.check(codeSize == code.size() && codeAt(obj, codeOffset, code),
               "COFF .text contains native bytes");
    const auto sym = field(obj, 8, 4);
    test.check(field(obj, 12, 4) == 1 && field(obj, sym + 12, 2) == 1 &&
               obj.at(sym + 16) == 2, "COFF external symbol");
    const std::string expected = (i386 ? "_" : "") + std::string(symbol);
    const auto stringTable = sym + 18;
    if (expected.size() <= 8) {
        test.check(std::string(reinterpret_cast<const char *>(obj.data() + sym),
                               expected.size()) == expected,
                   "COFF inline symbol");
    } else {
        test.check(field(obj, sym, 4) == 0 && field(obj, sym + 4, 4) == 4 &&
                   cstring(obj, stringTable + 4) == expected,
                   "COFF long symbol and string table");
    }
}

} // namespace

int main() {
    using namespace vietvm::native;
    Tester test;
    struct Case { const char *triple; bool small; bool little; unsigned machine; };
    const std::vector<Case> linuxTargets = {
        {"x86_64", false, true, 62}, {"aarch64", false, true, 183},
        {"riscv64", false, true, 243}, {"armv7", true, true, 40},
        {"i386", true, true, 3}, {"powerpc", true, false, 20},
        {"powerpc64", false, false, 21}, {"powerpc64le", false, true, 21},
        {"mips", true, false, 8}, {"mipsel", true, true, 8},
        {"mips64", false, false, 8}, {"mips64el", false, true, 8},
        {"loongarch64", false, true, 258}, {"s390x", false, false, 22},
        {"sparc", true, false, 2}, {"sparc64", false, false, 43},
    };
    test.check(linuxTargets.size() == registeredArchitectures().size(),
               "one ELF validation case for each registered ISA");
    for (const auto &variant : linuxTargets) {
        const auto parsed = parseTarget(std::string(variant.triple) + "-linux-gnu");
        test.check(bool(parsed.target), variant.triple);
        if (!parsed.target) continue;
        const RawLeaf leaf{RawLeafKind::AddI32Arguments, 0};
        const auto code = emitRawLeaf(*parsed.target, leaf);
        const auto obj = emitRawObject(*parsed.target, leaf, "cong");
        test.check(bool(code) && bool(obj), std::string(variant.triple) + " emit ELF");
        if (!code || !obj) continue;
        try {
            const std::string_view arch(variant.triple);
            const std::uint32_t flags =
                arch == "armv7" ? 0x05000000u :
                arch == "riscv64" ? 0x4u :
                arch == "loongarch64" ? 0x3u :
                arch == "powerpc64" ? 1u :
                arch == "powerpc64le" ? 2u : 0u;
            checkElf(test, obj.bytes, code.bytes, "cong", variant.small,
                     variant.little, variant.machine, flags,
                     arch == "powerpc64");
        } catch (const std::exception &error) {
            test.check(false, std::string(variant.triple) + ": " + error.what());
        }
    }
    for (const auto &arch : {"aarch64", "x86_64"}) {
        const auto parsed = parseTarget(std::string(arch) + "-apple-darwin");
        if (!parsed.target) { test.check(false, "MacOS target parser"); continue; }
        const RawLeaf leaf{RawLeafKind::ReturnConstantI32, -7};
        const auto code = emitRawLeaf(*parsed.target, leaf);
        const auto obj = emitRawObject(*parsed.target, leaf, "constant_long_name");
        test.check(bool(code) && bool(obj), "Mach-O emit");
        if (code && obj) try {
            checkMacho(test, obj.bytes, code.bytes, "constant_long_name",
                       std::string(arch) == "aarch64" ? 0x0100000c : 0x01000007);
        } catch (const std::exception &error) {
            test.check(false, std::string(arch) + ": " + error.what());
        }
    }
    for (const auto &arch : {"x86_64", "i386"}) {
        const auto parsed = parseTarget(std::string(arch) + "-windows-msvc");
        if (!parsed.target) { test.check(false, "Windows target parser"); continue; }
        const RawLeaf leaf{RawLeafKind::AddI32Arguments, 0};
        const auto code = emitRawLeaf(*parsed.target, leaf);
        const auto obj = emitRawObject(*parsed.target, leaf, "constant_long_name");
        test.check(bool(code) && bool(obj), "COFF emit");
        if (code && obj) try {
            checkCoff(test, obj.bytes, code.bytes, "constant_long_name",
                      std::string(arch) == "i386" ? 0x014c : 0x8664,
                      std::string(arch) == "i386");
        } catch (const std::exception &error) {
            test.check(false, std::string(arch) + ": " + error.what());
        }
    }
    const auto winArm = parseTarget("aarch64-pc-windows-msvc");
    test.check(bool(winArm.target) &&
                   !emitRawObject(*winArm.target,
                                  {RawLeafKind::AddI32Arguments, 0}, "cong"),
               "unsupported Windows AArch64 must fail explicitly");
    const auto linuxTarget = parseTarget("x86_64-linux-gnu");
    if (linuxTarget.target) {
        for (std::string_view bad : {"", "9bad", "bad;drop", "bad.name", "chữ"})
            test.check(!emitRawObject(*linuxTarget.target,
                                      {RawLeafKind::AddI32Arguments, 0}, bad),
                       "invalid exported symbol rejected");
    }
    if (test.failures) return 1;
    std::cout << "ELF/Mach-O/COFF raw objects: PASS\n";
    return 0;
}
