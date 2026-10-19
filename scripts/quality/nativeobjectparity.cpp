#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "frontend/keywords.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/native/raw_codegen.h"
#include "vpp/native/vpp_machine_bridge.h"

namespace {

using vietvm::native::MachineRelocation;
using vietvm::native::RawObjectResult;
using Bytes = std::vector<std::uint8_t>;

struct Tester {
    const std::filesystem::path &sourceBase;
    unsigned passed = 0;
    unsigned failed = 0;

    void compare(const std::string &triple, const Bytes &code,
                 const std::string &symbol,
                 const std::vector<MachineRelocation> &relocations = {},
                 bool expectSuccess = true) {
        const auto parsed = vietvm::native::parseTarget(triple);
        if (!parsed.target) {
            std::cerr << "FAIL " << triple << ": " << parsed.error << '\n';
            ++failed;
            return;
        }
        const RawObjectResult cpp = vietvm::native::emitRawObjectBytes(
            *parsed.target, code, symbol, relocations);
        const RawObjectResult vpp = vietvm::native::emitRawObjectBytesVpp(
            *parsed.target, code, symbol, relocations, sourceBase);
        const std::string label = triple + " / " + symbol +
            " / relocations=" + std::to_string(relocations.size());
        if (bool(cpp) != expectSuccess || bool(vpp) != expectSuccess) {
            std::cerr << "FAIL " << label << " C++: " << cpp.error
                      << ", V++: " << vpp.error << '\n';
            ++failed;
            return;
        }
        if (expectSuccess && cpp.bytes != vpp.bytes) {
            const auto mismatch = std::mismatch(
                cpp.bytes.begin(), cpp.bytes.end(),
                vpp.bytes.begin(), vpp.bytes.end());
            const auto offset = static_cast<std::size_t>(
                mismatch.first - cpp.bytes.begin());
            std::cerr << "FAIL " << label << " byte " << offset
                      << " (C++ size " << cpp.bytes.size()
                      << ", V++ size " << vpp.bytes.size() << ')';
            if (mismatch.first != cpp.bytes.end() &&
                mismatch.second != vpp.bytes.end()) {
                std::cerr << " C++=" << unsigned(*mismatch.first)
                          << " V++=" << unsigned(*mismatch.second);
            }
            std::cerr << '\n';
            ++failed;
            return;
        }
        ++passed;
        std::cout << "PASS " << label << '\n';
    }

    void leaf(const std::string &triple, const std::string &symbol,
              vietvm::native::RawLeafKind kind =
                  vietvm::native::RawLeafKind::ReturnConstantI32) {
        const auto parsed = vietvm::native::parseTarget(triple);
        if (!parsed.target) {
            std::cerr << "FAIL target " << triple << ": " << parsed.error << '\n';
            ++failed;
            return;
        }
        const auto code = vietvm::native::emitRawLeaf(*parsed.target, {kind, -7});
        if (!code) {
            std::cerr << "FAIL leaf " << triple << ": " << code.error << '\n';
            ++failed;
            return;
        }
        compare(triple, code.bytes, symbol);
    }

    // Verify that the combined V++ lowering/ISA/object production path emits
    // the same bytes as the independent C++ encoder and C++ object writer.
    // The standalone writer parity cases above do not exercise this path.
    void integrated(const std::string &triple, const std::string &fixture,
                    const std::string &function, bool leafFunction) {
        const std::string label = "integrated " + triple + " / " + function;
        try {
            const auto parsed = vietvm::native::parseTarget(triple);
            if (!parsed.target) throw std::runtime_error(parsed.error);
            const auto source = sourceBase / "src/tests" / fixture;
            std::ifstream file(source, std::ios::binary);
            if (!file) throw std::runtime_error("cannot open fixture: " + source.string());
            const std::string text(std::istreambuf_iterator<char>{file},
                                   std::istreambuf_iterator<char>{});
            vietvm::compiler::CompilationContext context;
            context.importResolutionBase = source.parent_path();
            context.currentSourceIdentity = source.lexically_normal().generic_u8string();
            const auto artifacts = vietvm::compiler::compilePipeline(
                context, text, keywordMap, false);

            Bytes machineBytes;
            std::vector<MachineRelocation> relocations;
            Bytes objectBytes;
            if (leafFunction) {
                const auto vpp = vietvm::native::compileRawLeafVpp(
                    *parsed.target, artifacts.ir, function, context.importResolutionBase, true);
                if (!vpp || !vpp.object)
                    throw std::runtime_error("V++ leaf: " + vpp.error + vpp.object.error);
                const auto cpp = vietvm::native::emitRawLeaf(*parsed.target, vpp.leaf);
                if (!cpp || cpp.bytes != vpp.emission.bytes)
                    throw std::runtime_error("leaf encoder C++/V++ differs: " + cpp.error);
                machineBytes = cpp.bytes;
                objectBytes = vpp.object.bytes;
            } else {
                const auto vpp = vietvm::native::compileMachineFunctionVpp(
                    *parsed.target, artifacts.ir, function, context.importResolutionBase, true);
                if (!vpp || !vpp.object)
                    throw std::runtime_error("V++ machine: " + vpp.error + vpp.object.error);
                const auto lowered = vietvm::native::lowerMachineFunction(artifacts.ir, function);
                if (!lowered) throw std::runtime_error("C++ lowering: " + lowered.error);
                const auto cpp = vietvm::native::emitMachineFunction(*parsed.target, lowered.function);
                if (!cpp) throw std::runtime_error("C++ encoder: " + cpp.error);
                if (cpp.bytes != vpp.emission.bytes ||
                    cpp.relocations.size() != vpp.emission.relocations.size())
                    throw std::runtime_error("machine encoder C++/V++ differs");
                for (std::size_t i = 0; i < cpp.relocations.size(); ++i)
                    if (cpp.relocations[i].offset != vpp.emission.relocations[i].offset ||
                        cpp.relocations[i].symbol != vpp.emission.relocations[i].symbol)
                        throw std::runtime_error("machine relocation C++/V++ differs");
                machineBytes = cpp.bytes;
                relocations = cpp.relocations;
                objectBytes = vpp.object.bytes;
            }
            const auto cppObject = vietvm::native::emitRawObjectBytes(
                *parsed.target, machineBytes, function, relocations);
            if (!cppObject || cppObject.bytes != objectBytes)
                throw std::runtime_error("object C++/V++ differs: " + cppObject.error);
            ++passed;
            std::cout << "PASS " << label << '\n';
        } catch (const std::exception &error) {
            ++failed;
            std::cerr << "FAIL " << label << ": " << error.what() << '\n';
        }
    }
};

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: nativeobjectparity <repository-root>\n";
        return 2;
    }
    Tester t{std::filesystem::path(argv[1])};
    for (const auto *arch : {
             "x86_64", "aarch64", "riscv64", "armv7", "i386",
             "powerpc", "powerpc64", "powerpc64le", "mips", "mipsel",
             "mips64", "mips64el", "loongarch64", "s390x",
             "sparc", "sparc64"}) {
        t.leaf(std::string(arch) + "-linux-gnu", "return_seven");
    }
    t.leaf("armv7-linux-gnueabihf", "hard_float");
    t.leaf("armv7-linux-musleabihf", "musl_hard_float");
    t.leaf("aarch64-apple-darwin", "hello_darwin");
    t.leaf("x86_64-apple-darwin", "hello_darwin");
    t.leaf("i386-windows-msvc", "coff_long_export_symbol");
    t.leaf("x86_64-windows-msvc", "coff_long_export_symbol");

    // CALL operands point at a 32-bit displacement (x86_64) or the whole
    // 32-bit BL instruction (AArch64). Include multiple undefined symbols,
    // and COFF names that must be encoded through the string table.
    const Bytes x86calls{0xe8, 0, 0, 0, 0, 0xe8, 0, 0, 0, 0, 0xc3};
    const std::vector<MachineRelocation> x86relocs{
        {1, "callee_long_symbol_one"}, {6, "callee_long_symbol_two"}};
    const Bytes armcalls{0, 0, 0, 0x94, 0, 0, 0, 0x94,
                         0xc0, 0x03, 0x5f, 0xd6};
    const std::vector<MachineRelocation> armrelocs{
        {0, "callee_long_symbol_one"}, {4, "callee_long_symbol_two"}};
    t.compare("x86_64-linux-gnu", x86calls, "caller", x86relocs);
    t.compare("aarch64-linux-gnu", armcalls, "caller", armrelocs);
    t.compare("x86_64-apple-darwin", x86calls, "caller", x86relocs);
    t.compare("aarch64-apple-darwin", armcalls, "caller", armrelocs);
    t.compare("x86_64-windows-msvc", x86calls,
              "caller_with_long_name", x86relocs);

    // Unsupported relocations and malformed input must fail explicitly.
    t.compare("i386-linux-gnu", x86calls, "bad_reloc", x86relocs, false);
    t.compare("i386-windows-msvc", x86calls, "bad_reloc", x86relocs, false);
    t.compare("x86_64-linux-gnu", {}, "empty", {}, false);
    t.compare("x86_64-linux-gnu", {0xc3}, "bad-name", {}, false);
    t.compare("x86_64-linux-gnu", {0xc3}, "valid", {{0, "call"}}, false);

    t.integrated("aarch64-apple-darwin", "kiem_tra_native_leaf.vi", "tra42", true);
    t.integrated("powerpc64-linux-gnu", "kiem_tra_native_leaf.vi", "tra42", true);
    t.integrated("riscv64-linux-gnu", "kiem_tra_native_leaf.vi", "tra42", true);
    t.integrated("i386-windows-msvc", "kiem_tra_native_leaf.vi", "tra42", true);
    t.integrated("aarch64-apple-darwin", "kiem_tra_native_calls.vi", "goi_bon", false);
    t.integrated("x86_64-linux-gnu", "kiem_tra_native_calls.vi", "goi_bon", false);
    t.integrated("x86_64-windows-msvc", "kiem_tra_native_calls.vi", "goi_bon", false);

    std::cout << "Object writer parity: " << t.passed << " passed, "
              << t.failed << " failed\n";
    return t.failed ? 1 : 0;
}
