#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vpp/native/raw_codegen.h"

namespace {

std::string hex(const std::vector<std::uint8_t> &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0xf]);
    }
    return result;
}

} // namespace

int main() {
    using namespace vietvm::native;
    int failed = 0;
    const auto expect = [&](bool okay, const std::string &label) {
        if (!okay) {
            std::cerr << "FAIL: " << label << '\n';
            ++failed;
        }
    };

    struct Vector { const char *arch; const char *constant; const char *add; const char *sub; };
    // ISA reference encodings; these are literal byte expectations, not
    // assembled via the encoder under test. Delay slots and endianness matter.
    const std::vector<Vector> vectors = {
        {"x86_64", "b82a000000c3", "89f801f0c3", "89f829f0c3"},
        {"aarch64", "400580520000a072c0035fd6", "0000010bc0035fd6", "0000014bc0035fd6"},
        {"riscv64", "370500001b05a50267800000", "3b05b50067800000", "3b05b54067800000"},
        {"armv7", "2a0000e3000040e31eff2fe1", "010080e01eff2fe1", "010040e01eff2fe1"},
        {"i386", "b82a000000c3", "8b44240403442408c3", "8b4424042b442408c3"},
        {"powerpc", "3c6000006063002a4e800020", "7c6322144e800020", "7c6418504e800020"},
        {"powerpc64", "3c6000006063002a4e800020", "7c6322144e800020", "7c6418504e800020"},
        {"powerpc64le", "0000603c2a0063602000804e", "1422637c2000804e", "5018647c2000804e"},
        {"mips", "3c0200003442002a03e0000800000000", "0085102103e0000800000000", "0085102303e0000800000000"},
        {"mipsel", "0000023c2a0042340800e00300000000", "211085000800e00300000000", "231085000800e00300000000"},
        {"mips64", "3c0200003442002a03e0000800000000", "0085102103e0000800000000", "0085102303e0000800000000"},
        {"mips64el", "0000023c2a0042340800e00300000000", "211085000800e00300000000", "231085000800e00300000000"},
        {"loongarch64", "0400001484a880032000004c", "841410002000004c", "841411002000004c"},
        {"s390x", "a729002a07fe", "1a2307fe", "1b2307fe"},
        {"sparc", "110000009012202a81c3e00801000000", "9002000981c3e00801000000", "9022000981c3e00801000000"},
        {"sparc64", "110000009012202a81c3e00801000000", "9002000981c3e00801000000", "9022000981c3e00801000000"},
    };
    expect(vectors.size() == registeredArchitectures().size(), "mọi CPU đã đăng ký có test");
    for (const auto &test : vectors) {
        const auto target = parseTarget(std::string(test.arch) + "-linux-gnu");
        expect(bool(target.target), std::string(test.arch) + " target Linux");
        if (!target.target) continue;
        const auto constant = emitRawLeaf(*target.target, {RawLeafKind::ReturnConstantI32, 42});
        expect(bool(constant) && hex(constant.bytes) == test.constant,
               std::string(test.arch) + " return i32 constant: " + hex(constant.bytes));
        const auto add = emitRawLeaf(*target.target, {RawLeafKind::AddI32Arguments, 0});
        expect(bool(add) && hex(add.bytes) == test.add,
               std::string(test.arch) + " add i32 parameters: " + hex(add.bytes));
        const auto sub = emitRawLeaf(*target.target, {RawLeafKind::SubI32Arguments, 0});
        expect(bool(sub) && hex(sub.bytes) == test.sub,
               std::string(test.arch) + " subtract i32 parameters: " + hex(sub.bytes));
    }

    for (const auto &abi : {
             std::pair<const char *, std::pair<const char *, const char *>>{
                 "x86_64-windows-msvc", {"89c801d0c3", "89c829d0c3"}},
             {"x86_64-apple-darwin", {"89f801f0c3", "89f829f0c3"}},
             {"i386-windows-msvc", {"8b44240403442408c3", "8b4424042b442408c3"}},
             {"aarch64-apple-darwin", {"0000010bc0035fd6", "0000014bc0035fd6"}},
         }) {
        const auto target = parseTarget(abi.first);
        expect(bool(target.target), std::string(abi.first) + " valid triple");
        if (!target.target) continue;
        const auto add = emitRawLeaf(*target.target, {RawLeafKind::AddI32Arguments, 0});
        expect(bool(add) && hex(add.bytes) == abi.second.first,
               std::string(abi.first) + " ABI add");
        const auto sub = emitRawLeaf(*target.target, {RawLeafKind::SubI32Arguments, 0});
        expect(bool(sub) && hex(sub.bytes) == abi.second.second,
               std::string(abi.first) + " ABI subtract");
    }
    const auto s390 = parseTarget("s390x-linux-gnu");
    if (s390.target) {
        for (const auto &caseValue : {
                 std::pair<std::int32_t, const char *>{32768, "c0210000800007fe"},
                 {2147483647, "c0217fffffff07fe"},
                 {-2147483647 - 1, "c0218000000007fe"},
                 {-32768, "a729800007fe"}}) {
            const auto encoding = emitRawLeaf(*s390.target,
                {RawLeafKind::ReturnConstantI32, caseValue.first});
            expect(encoding && hex(encoding.bytes) == caseValue.second,
                   "s390x signed i32 constant: " + hex(encoding.bytes));
        }
    } else expect(false, "s390x target available");

    const auto riscv = parseTarget("riscv64-linux-gnu");
    if (riscv.target) {
        for (const auto &caseValue : {
                 std::pair<std::int32_t, const char *>{-2147483647 - 1, "370500801b05050067800000"},
                 {2147483647, "370500801b05f5ff67800000"},
                 {2048, "371500001b05058067800000"},
                 {-2048, "370500001b05058067800000"}}) {
            const auto encoding = emitRawLeaf(*riscv.target,
                {RawLeafKind::ReturnConstantI32, caseValue.first});
            expect(encoding && hex(encoding.bytes) == caseValue.second,
                   "riscv64 signed i32 constant: " + hex(encoding.bytes));
        }
    } else expect(false, "riscv64 target available");

    // Assembly is a readable ISA representation of the same restricted leaf.
    const auto arm = parseTarget("aarch64-apple-darwin");
    if (arm.target) {
        const auto listing = emitRawAssembly(*arm.target,
            {RawLeafKind::AddI32Arguments, 0}, "cong");
        expect(bool(listing) && listing.text.find("_cong:\nadd w0, w0, w1\nret\n") !=
            std::string::npos, "macOS ARM64 readable assembly/ABI");
        const auto subtraction = emitRawAssembly(*arm.target,
            {RawLeafKind::SubI32Arguments, 0}, "tru");
        expect(bool(subtraction) && subtraction.text.find("_tru:\nsub w0, w0, w1\nret\n") !=
            std::string::npos, "macOS ARM64 subtraction assembly/ABI");
        expect(!emitRawAssembly(*arm.target,
            {RawLeafKind::AddI32Arguments, 0}, "bad; .word 0"),
            "reject injected assembly symbol");

        // Exercise the highest available register and the next-slot fallback.
        // A fixed slot assignment must remain within the platform's volatile
        // registers; no platform/callee-saved register may be silently used.
        MachineFunction boundary;
        boundary.parameterCount = 4;
        boundary.slotCount = 14;
        boundary.maxStackDepth = 2;
        boundary.instructions = {
            {MachineOpcode::LoadSlot, 0, 0, 0},
            {MachineOpcode::StoreSlot, 0, 13, 1},
            {MachineOpcode::Drop, 0, 0, 1},
            {MachineOpcode::LoadSlot, 0, 13, 0},
            {MachineOpcode::LoadSlot, 0, 1, 1},
            {MachineOpcode::Add, 0, 0, 2},
            {MachineOpcode::Return, 0, 0, 1},
            {MachineOpcode::Trap, 0, 0, 0},
        };
        const auto registers = emitMachineFunction(*arm.target, boundary);
        expect(registers && registers.assembly.find("sp") == std::string::npos &&
               registers.assembly.find("w17") != std::string::npos &&
               registers.assembly.find("w18") == std::string::npos &&
               registers.assembly.find("w19") == std::string::npos,
               "AArch64 leaf at register budget avoids stack and reserved registers");
        boundary.slotCount = 15;
        const auto spilled = emitMachineFunction(*arm.target, boundary);
        expect(spilled && spilled.assembly.find("stp x29, x30") != std::string::npos &&
               spilled.assembly.find("[sp, #") != std::string::npos,
               "AArch64 leaf beyond register budget retains stack frame");
        boundary.slotCount = 14;
        boundary.instructions[5] = {MachineOpcode::Call, 0, 2, 2, "callee"};
        const auto caller = emitMachineFunction(*arm.target, boundary);
        expect(caller && caller.relocations.size() == 1 &&
               caller.assembly.find("stp x29, x30") != std::string::npos &&
               caller.assembly.find("ldp x29, x30") != std::string::npos,
               "AArch64 function containing CALL preserves frame and LR");
    }
    const auto win = parseTarget("x86_64-windows-msvc");
    if (win.target) {
        const auto listing = emitRawAssembly(*win.target,
            {RawLeafKind::AddI32Arguments, 0}, "cong");
        expect(bool(listing) && listing.text.find("mov eax, ecx\nadd eax, edx\nret\n") !=
            std::string::npos, "Win64 ECX/EDX argument ABI");
        const auto subtraction = emitRawAssembly(*win.target,
            {RawLeafKind::SubI32Arguments, 0}, "tru");
        expect(bool(subtraction) && subtraction.text.find("mov eax, ecx\nsub eax, edx\nret\n") !=
            std::string::npos, "Win64 subtraction ECX/EDX ABI");
    }
    const auto winArm64 = parseTarget("aarch64-pc-windows-msvc");
    expect(bool(winArm64.target), "Windows ARM64 triple hợp lệ");
    if (winArm64.target) {
        expect(!emitRawLeaf(*winArm64.target,
                            {RawLeafKind::AddI32Arguments, 0}),
               "Windows ARM64 raw ABI chưa có phải bị từ chối");
        expect(!emitRawAssembly(*winArm64.target,
                                {RawLeafKind::AddI32Arguments, 0}, "cong"),
               "Windows ARM64 assembly ABI chưa có phải bị từ chối");
    }

    // CFG validation must reject different evaluation-stack heights reaching
    // the same instruction; this cannot be checked with a linear counter.
    MachineFunction badJoin;
    badJoin.maxStackDepth = 1;
    badJoin.instructions = {
        {MachineOpcode::PushConstant, 1, 0, 0},
        {MachineOpcode::BranchZero, 0, 3, 1},
        {MachineOpcode::PushConstant, 7, 0, 0},
        {MachineOpcode::Return, 0, 0, 1},
        {MachineOpcode::Trap, 0, 0, 0},
    };
    for (const char *name : {"x86_64-linux-gnu", "aarch64-linux-gnu"}) {
        const auto arch = parseTarget(name);
        expect(bool(arch.target), std::string(name) + " CFG target");
        if (!arch.target) continue;
        const auto invalid = emitMachineFunction(*arch.target, badJoin);
        expect(!invalid && invalid.error.find("stack") != std::string::npos,
               std::string(name) + " reject inconsistent CFG stack at join");
        auto outOfBounds = badJoin;
        outOfBounds.instructions[1].operand = 100;
        expect(!emitMachineFunction(*arch.target, outOfBounds),
               std::string(name) + " reject invalid branch target");
        MachineFunction fallthrough;
        fallthrough.maxStackDepth = 1;
        fallthrough.instructions = {{MachineOpcode::PushConstant, 42, 0, 0}};
        expect(!emitMachineFunction(*arch.target, fallthrough),
               std::string(name) + " reject falling off function end");
    }

    // Construct IR directly: malformed/nested effects must be rejected by the
    // lowering even when they contain a valid return later.
    vietvm::compiler::IrProgram ir;
    vietvm::compiler::IrValue constant;
    constant.id = 0;
    constant.opcode = vietvm::compiler::IrValueOpcode::ConstInt;
    constant.text = "42";
    ir.values.push_back(constant);
    vietvm::compiler::IrInstruction ret;
    ret.opcode = vietvm::compiler::IrOpcode::Return;
    ret.expressionRoots.push_back(0);
    vietvm::compiler::IrInstruction block;
    block.opcode = vietvm::compiler::IrOpcode::Block;
    block.children.push_back(ret);
    vietvm::compiler::IrInstruction function;
    function.opcode = vietvm::compiler::IrOpcode::DefineFunction;
    function.declarationName = "tra42";
    function.children.push_back(block);
    ir.instructions.push_back(function);
    expect(bool(lowerRawLeaf(ir, "tra42")), "lower valid constant IR");
    expect(!lowerRawLeaf(ir, "unknown"), "reject unknown function");
    ir.instructions[0].children[0].children[0].unsupportedDirectRegion = true;
    expect(!lowerRawLeaf(ir, "tra42"), "reject unsupported direct IR region");
    ir.instructions[0].children[0].children[0].unsupportedDirectRegion = false;
    ir.instructions[0].children[0].children.push_back(ret);
    expect(!lowerRawLeaf(ir, "tra42"), "reject multiple returns");

    // Direct IR lowering must preserve operand order for subtraction.
    ir.instructions[0].children[0].children.pop_back();
    ir.instructions[0].declarationName = "tru";
    vietvm::compiler::IrParameter first;
    first.name = "a";
    first.symbolId = 10;
    vietvm::compiler::IrParameter second;
    second.name = "b";
    second.symbolId = 11;
    ir.instructions[0].parameters = {first, second};
    vietvm::compiler::IrValue lhs;
    lhs.id = 1;
    lhs.opcode = vietvm::compiler::IrValueOpcode::LoadName;
    lhs.symbolId = first.symbolId;
    vietvm::compiler::IrValue rhs = lhs;
    rhs.id = 2;
    rhs.symbolId = second.symbolId;
    vietvm::compiler::IrValue sub;
    sub.id = 3;
    sub.opcode = vietvm::compiler::IrValueOpcode::Binary;
    sub.text = "-";
    sub.operands = {1, 2};
    ir.values.push_back(lhs);
    ir.values.push_back(rhs);
    ir.values.push_back(sub);
    ir.instructions[0].children[0].children[0].expressionRoots = {3};
    const auto lowered = lowerRawLeaf(ir, "tru");
    expect(bool(lowered) && lowered.leaf.kind == RawLeafKind::SubI32Arguments,
           "lower subtract parameters in source order");
    ir.values[3].operands = {2, 1};
    expect(!lowerRawLeaf(ir, "tru"), "reject reversed argument subtraction");
    ir.values[3].operands = {1, 2};
    ir.instructions[0].parameters[0].hasDefault = true;
    expect(!lowerRawLeaf(ir, "tru"), "reject default argument in native ABI");
    ir.instructions[0].parameters[0].hasDefault = false;
    ir.values[3].text = "+";
    ir.values[3].operands = {2, 1};
    const auto reversedAdd = lowerRawLeaf(ir, "tru");
    expect(bool(reversedAdd) && reversedAdd.leaf.kind == RawLeafKind::AddI32Arguments,
           "lower b + a using commutativity");

    // Nested pure arithmetic must fold to one signed-i32 return value.
    vietvm::compiler::IrProgram folded;
    const auto pushValue = [&](vietvm::compiler::IrValueOpcode opcode,
                               std::string spelling,
                               std::vector<vietvm::compiler::IrValueId> operands = {}) {
        vietvm::compiler::IrValue value;
        value.id = folded.values.size();
        value.opcode = opcode;
        value.text = std::move(spelling);
        value.operands = std::move(operands);
        folded.values.push_back(std::move(value));
        return folded.values.size() - 1;
    };
    using Op = vietvm::compiler::IrValueOpcode;
    const auto nine = pushValue(Op::ConstInt, "9");
    const auto three = pushValue(Op::ConstInt, "3");
    const auto eight = pushValue(Op::ConstInt, "8");
    const auto four = pushValue(Op::ConstInt, "4");
    const auto sum = pushValue(Op::Binary, "+", {nine, three});
    const auto diff = pushValue(Op::Binary, "-", {eight, four});
    const auto product = pushValue(Op::Binary, "*", {sum, diff});
    vietvm::compiler::IrInstruction nestedReturn;
    nestedReturn.opcode = vietvm::compiler::IrOpcode::Return;
    nestedReturn.expressionRoots = {product};
    vietvm::compiler::IrInstruction nestedBlock;
    nestedBlock.opcode = vietvm::compiler::IrOpcode::Block;
    nestedBlock.children.push_back(nestedReturn);
    vietvm::compiler::IrInstruction nestedFunction;
    nestedFunction.opcode = vietvm::compiler::IrOpcode::DefineFunction;
    nestedFunction.declarationName = "tinh_hang";
    nestedFunction.children.push_back(nestedBlock);
    folded.instructions.push_back(nestedFunction);
    const auto foldedResult = lowerRawLeaf(folded, "tinh_hang");
    expect(bool(foldedResult) &&
               foldedResult.leaf.kind == RawLeafKind::ReturnConstantI32 &&
               foldedResult.leaf.constant == 48,
           "fold nested add/sub/mul safely");

    const auto big = pushValue(Op::ConstInt, "2147483647");
    const auto one = pushValue(Op::ConstInt, "1");
    const auto overflow = pushValue(Op::Binary, "+", {big, one});
    const auto overflowThenSubtract = pushValue(Op::Binary, "-", {overflow, one});
    folded.instructions[0].children[0].children[0].expressionRoots = {overflow};
    expect(!lowerRawLeaf(folded, "tinh_hang"), "reject i32 overflow");
    folded.instructions[0].children[0].children[0].expressionRoots = {overflowThenSubtract};
    expect(!lowerRawLeaf(folded, "tinh_hang"), "reject intermediate i32 overflow");
    const auto negativeMinLiteral = pushValue(Op::ConstInt, "2147483648");
    const auto negativeMin = pushValue(Op::Unary, "-", {negativeMinLiteral});
    folded.instructions[0].children[0].children[0].expressionRoots = {negativeMin};
    const auto minResult = lowerRawLeaf(folded, "tinh_hang");
    expect(bool(minResult) && minResult.leaf.constant == INT32_MIN,
           "allow direct unary minus of 2147483648");
    const auto negatedMin = pushValue(Op::Unary, "-", {negativeMin});
    folded.instructions[0].children[0].children[0].expressionRoots = {negatedMin};
    expect(!lowerRawLeaf(folded, "tinh_hang"), "reject negating INT32_MIN");
    const auto divide = pushValue(Op::Binary, "/", {eight, four});
    folded.instructions[0].children[0].children[0].expressionRoots = {divide};
    expect(!lowerRawLeaf(folded, "tinh_hang"), "reject arithmetic not in subset");
    const auto selfReference = pushValue(Op::Unary, "-", {});
    folded.values[selfReference].operands = {selfReference};
    folded.instructions[0].children[0].children[0].expressionRoots = {selfReference};
    expect(!lowerRawLeaf(folded, "tinh_hang"), "reject cyclic malformed IR");

    if (failed) return 1;
    std::cout << "Raw machine-code vectors: PASS\n";
    return 0;
}
