// Independent parity oracle: call the retained C++ machine encoder directly.
// The production CLI uses emitMachineFunctionVpp, so querying that CLI alone
// would compare V++ against itself rather than detect regressions.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "frontend/keywords.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/native/raw_codegen.h"
#include "vpp/native/target.h"
#include "vpp/native/vpp_machine_bridge.h"

namespace {

std::string readSource(const std::filesystem::path &source) {
    std::ifstream in(source, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read fixture: " + source.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string toHex(const std::vector<std::uint8_t> &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        output += digits[byte >> 4];
        output += digits[byte & 0x0f];
    }
    return output;
}

void verifyFunctions(const std::filesystem::path &source,
                     const std::vector<std::string> &names,
                     const std::vector<std::string> &triples) {
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = source.parent_path().empty()
        ? std::filesystem::current_path() : source.parent_path();
    context.currentSourceIdentity = source.lexically_normal().generic_u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, readSource(source), keywordMap, false);

    for (const auto &name : names) {
        const auto lowered = vietvm::native::lowerMachineFunction(artifacts.ir, name);
        if (!lowered) throw std::runtime_error(name + ": " + lowered.error);
        for (const auto &triple : triples) {
            const auto target = vietvm::native::parseTarget(triple);
            if (!target.target)
                throw std::runtime_error(triple + ": " + target.error);
            const auto encoded = vietvm::native::emitMachineFunction(*target.target,
                                                                       lowered.function);
            if (!encoded)
                throw std::runtime_error(triple + "/" + name + ": " + encoded.error);
            const auto key = name + "#" + std::string(vietvm::native::architectureName(target.target->arch)) +
                "#" + (target.target->os == vietvm::native::OperatingSystem::MacOS ? "macos" :
                       target.target->os == vietvm::native::OperatingSystem::Windows ? "windows" : "linux");
            std::cout << key << ':' << toHex(encoded.bytes) << '\n';
            for (const auto &reloc : encoded.relocations)
                std::cout << "R#" << key << ':' << reloc.offset << ':' << reloc.symbol << '\n';
        }
    }
}

// Compare the lowering output before either backend encodes bytes. An encoder
// parity test alone can miss metadata and CFG mistakes shared by both paths.
void verifyLowering(const vietvm::compiler::IrProgram &ir,
                    const std::filesystem::path &source,
                    const std::string &name, bool expectSuccess) {
    const auto cpp = vietvm::native::lowerMachineFunction(ir, name);
    const auto vpp = vietvm::native::lowerMachineFunctionVpp(ir, name, source.parent_path());
    if (static_cast<bool>(cpp) != expectSuccess ||
        static_cast<bool>(vpp) != expectSuccess) {
        throw std::runtime_error(name + ": unexpected lowering result; C++=" +
                                 (cpp ? "success" : cpp.error) + "; V++=" +
                                 (vpp ? "success" : vpp.error));
    }
    if (!expectSuccess) return;
    const auto &a = cpp.function;
    const auto &b = vpp.function;
    if (a.parameterCount != b.parameterCount || a.slotCount != b.slotCount ||
        a.maxStackDepth != b.maxStackDepth ||
        a.instructions.size() != b.instructions.size()) {
        throw std::runtime_error(name + ": MachineFunction frame/instruction count diverged");
    }
    for (std::size_t i = 0; i < a.instructions.size(); ++i) {
        const auto &x = a.instructions[i];
        const auto &y = b.instructions[i];
        if (x.opcode != y.opcode || x.immediate != y.immediate ||
            x.operand != y.operand || x.depthBefore != y.depthBefore ||
            x.symbol != y.symbol) {
            throw std::runtime_error(name + ": MachineInstruction differs at index " +
                                     std::to_string(i) + " (opcode/immediate/operand/depth/symbol)");
        }
    }
}

void verifyLoweringFixture(const std::filesystem::path &source,
                           const std::vector<std::string> &successes,
                           const std::vector<std::string> &failures) {
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = source.parent_path().empty()
        ? std::filesystem::current_path() : source.parent_path();
    context.currentSourceIdentity = source.lexically_normal().generic_u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, readSource(source), keywordMap, false);
    for (const auto &name : successes) {
        verifyLowering(artifacts.ir, source, name, true);
        std::cout << "PASS " << name << '\n';
    }
    for (const auto &name : failures) {
        verifyLowering(artifacts.ir, source, name, false);
        std::cout << "REJECT " << name << '\n';
    }
}

// Build numeric edge cases as IR: the frontend correctly rejects some of
// these literals before Machine IR lowering, so a source fixture alone cannot
// exercise the lowerer's overflow/rejection contract.
using V = vietvm::compiler::IrValueOpcode;
using ValueSpec = std::tuple<V, std::string, std::vector<std::size_t>>;

void verifySyntheticI32(const std::string &name,
                        const std::vector<ValueSpec> &specs,
                        bool expectSuccess,
                        const std::filesystem::path &sourceBase) {
    using namespace vietvm::compiler;
    IrProgram ir;
    for (const auto &[opcode, token, operands] : specs) {
        IrValue value;
        value.id = ir.values.size();
        value.opcode = opcode;
        value.text = token;
        value.operands = operands;
        ir.values.push_back(std::move(value));
    }
    IrInstruction ret;
    ret.opcode = IrOpcode::Return;
    ret.expressionRoots.push_back(ir.values.size() - 1);
    IrInstruction body;
    body.opcode = IrOpcode::Block;
    body.children.push_back(std::move(ret));
    IrInstruction definition;
    definition.opcode = IrOpcode::DefineFunction;
    definition.declarationName = name;
    definition.children.push_back(std::move(body));
    ir.instructions.push_back(std::move(definition));
    verifyLowering(ir, sourceBase, name, expectSuccess);
    std::cout << (expectSuccess ? "PASS " : "REJECT ") << name << '\n';
}

void verifySyntheticBoundaries(const std::filesystem::path &sourceBase) {
    verifySyntheticI32("synthetic_min", {{V::ConstInt, "2147483648", {}},
                                      {V::Unary, "-", {0}}}, true, sourceBase);
    verifySyntheticI32("synthetic_max", {{V::ConstInt, "2147483647", {}}},
                       true, sourceBase);
    verifySyntheticI32("synthetic_overflow_add", {{V::ConstInt, "2147483647", {}},
                                               {V::ConstInt, "1", {}},
                                               {V::Binary, "+", {0, 1}}},
                       false, sourceBase);
    verifySyntheticI32("synthetic_overflow_mul", {{V::ConstInt, "2147483647", {}},
                                               {V::ConstInt, "2147483647", {}},
                                               {V::Binary, "*", {0, 1}}},
                       false, sourceBase);
    verifySyntheticI32("synthetic_out_of_range", {{V::ConstInt, "2147483648", {}}},
                       false, sourceBase);
    verifySyntheticI32("synthetic_overflow_intermediate",
                       {{V::ConstInt, "2147483647", {}}, {V::ConstInt, "1", {}},
                        {V::Binary, "+", {0, 1}}, {V::Binary, "-", {2, 1}}},
                       false, sourceBase);
    verifySyntheticI32("synthetic_neg_overflow",
                       {{V::ConstInt, "2147483648", {}}, {V::Unary, "-", {0}},
                        {V::Unary, "-", {1}}}, false, sourceBase);
}

// Both the leaf classifier and ISA encoder are independently implemented in
// C++ and V++; compare them before exercising the production CLI/object path.
void verifyLeafParity(const std::filesystem::path &source) {
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = source.parent_path().empty()
        ? std::filesystem::current_path() : source.parent_path();
    context.currentSourceIdentity = source.lexically_normal().generic_u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, readSource(source), keywordMap, false);
    const std::vector<std::string> targets = {
        "x86_64-linux-gnu", "aarch64-linux-gnu", "riscv64-linux-gnu",
        "armv7-linux-gnueabihf", "i386-linux-gnu", "powerpc-linux-gnu",
        "powerpc64-linux-gnu", "powerpc64le-linux-gnu", "mips-linux-gnu",
        "mipsel-linux-gnu", "mips64-linux-gnu", "mips64el-linux-gnu",
        "loongarch64-linux-gnu", "s390x-linux-gnu", "sparc-linux-gnu",
        "sparc64-linux-gnu", "x86_64-windows-msvc", "aarch64-apple-darwin",
    };
    const std::vector<std::string> valid = {
        "tra42", "tra_am", "tra_i32", "tra_i32_am",
        "tra_i32_am_ngoac", "tinh_hang", "tinh_am", "tinh_sau",
        "cong", "cong_dao", "tru",
    };
    const std::vector<std::string> invalid = {
        "tru_dao", "hang_tran", "hang_tran_trung_gian", "hang_chia", "chua_ho_tro",
    };
    std::size_t verified = 0;
    for (const auto &name : valid) {
        const auto cpp = vietvm::native::lowerRawLeaf(artifacts.ir, name);
        const auto vpp = vietvm::native::lowerRawLeafVpp(artifacts.ir, name, context.importResolutionBase);
        if (!cpp || !vpp || cpp.leaf.kind != vpp.leaf.kind ||
            cpp.leaf.constant != vpp.leaf.constant) {
            throw std::runtime_error(name + ": leaf lowering differs; C++=" +
                                     (cpp ? "success" : cpp.error) + "; V++=" +
                                     (vpp ? "success" : vpp.error));
        }
        for (const auto &triple : targets) {
            const auto parsed = vietvm::native::parseTarget(triple);
            if (!parsed.target) throw std::runtime_error(triple + ": " + parsed.error);
            const auto a = vietvm::native::emitRawLeaf(*parsed.target, cpp.leaf);
            const auto b = vietvm::native::emitRawLeafVpp(*parsed.target, vpp.leaf,
                                                         context.importResolutionBase);
            if (!a || !b || a.bytes != b.bytes) {
                throw std::runtime_error(name + "/" + triple + ": leaf byte mismatch; C++=" +
                                         (a ? toHex(a.bytes) : a.error) + "; V++=" +
                                         (b ? toHex(b.bytes) : b.error));
            }
            ++verified;
        }
    }
    for (const auto &name : invalid) {
        const auto cpp = vietvm::native::lowerRawLeaf(artifacts.ir, name);
        const auto vpp = vietvm::native::lowerRawLeafVpp(artifacts.ir, name,
                                                        context.importResolutionBase);
        if (cpp || vpp)
            throw std::runtime_error(name + ": unsupported leaf was accepted");
    }
    std::cout << "Leaf C++/V++ lowerer parity: " << valid.size() + invalid.size()
              << "/" << valid.size() + invalid.size() << "\n";
    std::cout << "Leaf C++/V++ ISA bytes: " << verified << "/" << verified << "\n";
}

void verifyTargetParity(const std::filesystem::path &source) {
    std::vector<std::string> accepted;
    for (const auto &entry : vietvm::native::registeredArchitectures())
        accepted.emplace_back(std::string(entry.name) + "-unknown-linux-gnu");
    const std::vector<std::string> aliases = {
        "AMD64-linux", "arm64-apple-darwin", "rv64-linux-musl",
        "i686-linux-gnu", "armv7l-linux-gnueabihf", "ppc64le-linux-gnu",
        "mips64el-linux-gnu", "x64-pc-windows-msvc", "x86_64-w64-mingw32",
        "x86_64-apple-darwin", "aarch64-pc-windows-msvc",
        "i386-pc-windows-msvc", "armv7-linux-musleabihf",
    };
    accepted.insert(accepted.end(), aliases.begin(), aliases.end());
    const std::vector<std::string> rejected = {
        "riscv128-linux-gnu", "aarch64", "aarch64-plan9",
        "riscv64-linux-msvc", "x86_64-windows-musl",
        "aarch64-apple-darwin-gnu", "mips64-windows-msvc",
        "x86_64-linux-unknown", "-linux-gnu", "",
    };
    for (const auto &triple : accepted) {
        const auto cpp = vietvm::native::parseTarget(triple);
        const auto vpp = vietvm::native::parseTargetVpp(triple, source.parent_path());
        if (!cpp.target || !vpp.target)
            throw std::runtime_error(triple + ": C++/V++ target parser refused valid triple: " +
                                     (cpp.target ? "" : cpp.error) + " / " + vpp.error);
        const auto &a = *cpp.target;
        const auto &b = *vpp.target;
        if (a.arch != b.arch || a.os != b.os || a.abi != b.abi ||
            a.objectFormat != b.objectFormat ||
            vietvm::native::canonicalTriple(a) != vietvm::native::canonicalTriple(b))
            throw std::runtime_error(triple + ": C++/V++ target metadata diverged");
    }
    for (const auto &triple : rejected) {
        const auto cpp = vietvm::native::parseTarget(triple);
        const auto vpp = vietvm::native::parseTargetVpp(triple, source.parent_path());
        if (cpp.target || vpp.target || cpp.error.empty() || vpp.error.empty())
            throw std::runtime_error(triple + ": unsupported target accepted or error missing");
    }
    std::cout << "C++/V++ target triple parity: " << accepted.size() + rejected.size()
              << "/" << accepted.size() + rejected.size() << "\n";
}

} // namespace

int main(int argc, char **argv) {
    const bool lowering = argc == 5 && std::string(argv[1]) == "--lowering-parity";
    const bool leafParity = argc == 3 && std::string(argv[1]) == "--leaf-parity";
    const bool targetParity = argc == 3 && std::string(argv[1]) == "--target-parity";
    if (argc != 3 && !lowering) {
        std::cerr << "Usage: vpp-native-machine-cpp-oracle <control-flow.vi> <calls.vi>\n"
                  << "   or: vpp-native-machine-cpp-oracle --lowering-parity <control-flow.vi> <calls.vi> <boundaries.vi>\n"
                  << "   or: vpp-native-machine-cpp-oracle --leaf-parity <leaf.vi>\n";
        return 2;
    }
    try {
        if (targetParity) {
            verifyTargetParity(argv[2]);
            return 0;
        }
        if (leafParity) {
            verifyLeafParity(argv[2]);
            return 0;
        }
        if (lowering) {
            verifyLoweringFixture(argv[2],
                {"tru_dao", "tong_bon", "dieu_kien", "max_i32", "tong",
                 "bo_qua", "phan_loai", "tim_dau", "phu_dinh"}, {});
            verifyLoweringFixture(argv[3],
                {"goi_bon", "goi_muoi", "tong_muoi", "nhan_sau", "tinh_vong", "ket_hop"}, {});
            verifyLoweringFixture(argv[4],
                {"min_i32", "max_i32_const", "add_limit", "neg_limit"}, {});
            verifySyntheticBoundaries(argv[4]);
            std::cout << "Machine IR C++/V++ lowering parity: 26/26 cases\n";
            return 0;
        }
        const std::vector<std::string> targets = {
            "aarch64-apple-darwin", "aarch64-linux-gnu",
            "x86_64-linux-gnu", "x86_64-windows-msvc",
        };
        verifyFunctions(argv[1], {"tru_dao", "tong_bon", "dieu_kien", "max_i32", "tong"}, targets);
        verifyFunctions(argv[2], {"goi_bon", "goi_muoi", "tong_muoi"}, targets);
    } catch (const std::exception &e) {
        std::cerr << "C++ machine oracle: " << e.what() << '\n';
        return 1;
    }
}
