#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vpp/runtime/error.h"
#include "vpp/runtime/vm.h"
#include "vpp/runtime/vm_fixture.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testVerificationCacheAndInvalidation() {
    const std::vector<Instruction> root = {
        {OP_BIEN_SO, 42, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    VM vm(root, {});
    vm.run();
    expect(vm.verificationPassCount() == 1,
           "lần chạy đầu verify root đúng một lần");

    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 1,
           "chạy lại cùng generation không verify lại");

    vm.setFunctions({{7, {{999, 0, 0, 0}}}}, {});
    vm.resetExecution();
    bool invalidRejected = false;
    try {
        vm.run();
    } catch (const vietvm::runtime::RuntimeError &) {
        invalidRejected = true;
    }
    expect(invalidRejected, "function bytecode lỗi bị verifier từ chối");
    expect(vm.verificationPassCount() == 2,
           "generation mới chỉ mở một verification pass dù function lỗi");

    vm.setFunctions({{7, {{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}}}, {});
    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 3,
           "valid → invalid → valid tạo generation verifier độc lập");

    vm.setFunctions({{7, {{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}}}, {{11, 7}});
    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 4,
           "đổi function metadata làm invalid cache verifier");

    vm.setFunctions({}, {});
    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 5,
           "xóa function body làm invalid cache verifier");
}

void testModuleVerificationPrecedesSideEffects() {
    VM vm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    std::string output;
    vm.setOutputSink([&](const std::string &value) { output += value; });
    expect(vm.addModuleInitializer(
               "valid-first",
               {{OP_BIEN_SO, 9, 0, 0},
                {OP_IN, 0, 0, 0},
                {OP_DUNG_CHUONG_TRINH, 0, 0, 0}}),
           "thêm module hợp lệ");
    expect(vm.addModuleInitializer("invalid-second", {{999, 0, 0, 0}}),
           "thêm module lỗi để hardening");

    bool rejected = false;
    try {
        vm.run();
    } catch (const vietvm::runtime::RuntimeError &) {
        rejected = true;
    }
    expect(rejected, "module initializer lỗi bị từ chối");
    expect(output.empty(),
           "verifier quét toàn bộ initializer trước side effect module trước đó");
}

void testPeriodicGcKeepsCapacity() {
    VM vm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    VMRuntimeFixture fixture(vm);
    fixture.reserveStack(4096);
    const std::size_t reserved = fixture.stackCapacity();
    expect(reserved >= 4096, "fixture dự trữ stack capacity cho GC test");

    fixture.collectGarbage();
    expect(fixture.stackCapacity() == reserved,
           "periodic GC không shrink stack capacity");

    fixture.collectGarbageAndTrim();
    expect(fixture.stackCapacity() <= reserved,
           "explicit trim không làm tăng stack capacity");
}

void testLegacyIntrinsicUsesOpcodeDispatcher() {
    // Legacy OP_GOI by name must reach exactly the same primitives as OP_VM_*.
    // Cover collection, OS foundation, representation, network and file errors.
    VM vm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
          {"do_dai", "ten_nen_tang_vm", "chuoi_bytes_vm",
           "dns_phan_giai_vm", "io_doc_file_vm"});
    VMRuntimeFixture fixture(vm);

    fixture.push(make_string_value("Việt"));
    fixture.executeCall({OP_GOI, 1, -1, 0});
    expect(std::holds_alternative<int>(fixture.top()) &&
               std::get<int>(fixture.top()) == 4,
           "OP_GOI tên cũ dùng chung opcode độ dài UTF-8");

    fixture.clearStack();
    fixture.executeCall({OP_GOI, 0, -2, 0});
    expect(std::holds_alternative<std::string>(fixture.top()) &&
               !std::get<std::string>(fixture.top()).empty(),
           "OP_GOI tên cũ gọi được primitive nền tảng không đối số");

    fixture.clearStack();
    fixture.push(make_string_value("AB"));
    fixture.executeCall({OP_GOI, 1, -3, 0});
    const bool bytesOk = std::holds_alternative<ListHandle>(fixture.top()) &&
        std::get<ListHandle>(fixture.top()) != nullptr &&
        std::get<ListHandle>(fixture.top())->elements.size() == 2 &&
        std::get<int>(std::get<ListHandle>(fixture.top())->elements[0]) == 65;
    expect(bytesOk, "OP_GOI tên cũ dùng chung opcode chuyển biểu diễn bytes");

    fixture.clearStack();
    fixture.push(make_int_value(42));
    bool dnsCatchable = false;
    try {
        fixture.executeCall({OP_GOI, 1, -4, 0});
    } catch (const vietvm::runtime::LanguageException &) {
        dnsCatchable = true;
    }
    expect(dnsCatchable, "lỗi DNS từ OP_GOI cũ bắt được bằng thử/bắt V++");

    fixture.clearStack();
    fixture.push(make_string_value("__vpp_nonexistent_legacy_intrinsic_test__/missing"));
    bool fileRuntimeError = false;
    try {
        fixture.executeCall({OP_GOI, 1, -5, 0});
    } catch (const vietvm::runtime::RuntimeError &) {
        fileRuntimeError = true;
    }
    expect(fileRuntimeError, "lỗi đọc tệp vẫn là runtime diagnostic");
}

void testLegacyNetworkErrorCatchMatchesDirectOpcode() {
    const auto runCatch = [](bool legacy) {
        const std::vector<Instruction> code = {
            {OP_THU, 4, -1, 0},
            {OP_BIEN_SO, 42, 0, 0},
            {legacy ? OP_GOI : OP_VM_DNS_PHAN_GIAI, legacy ? 1 : 0,
             legacy ? -1 : 0, 0},
            {OP_THU_KET_THUC, 6, 0, 0},
            {OP_BAT_LOI, 0, -1, 0},
            {OP_BIEN_SO, 1, 0, 0},
            {OP_IN, 0, 0, 0},
            {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
        };
        VM vm(code, {"dns_phan_giai_vm"});
        std::string output;
        vm.setOutputSink([&](const std::string &text) { output += text; });
        vm.run(); // Also exercises verifier and VM exception unwinding.
        return output;
    };
    const std::string direct = runCatch(false);
    const std::string legacy = runCatch(true);
    expect(!direct.empty() && direct == legacy && direct.find('1') != std::string::npos,
           "try/catch DNS qua OP_GOI cũ và OP_VM_DNS_PHAN_GIAI giống nhau");
}

void testJitFunctionsBranchesAndIntrinsics() {
    // Root -> V++ function -> loop -> return -> VM intrinsic -> catchable error.
    // The old linear JIT rejected this entire program because of OP_GOI.
    const std::vector<Instruction> root = {
        {OP_BIEN_SO, 4, 0, 0},
        {OP_GOI, 1, 7, 0},
        {OP_IN, 0, 0, 0},
        {OP_CHUOI, 0, 0, 0},
        {OP_VM_LENGTH, 0, 0, 0},
        {OP_IN, 0, 0, 0},
        {OP_THU, 10, -1, 0},
        {OP_BIEN_SO, 42, 0, 0},
        {OP_VM_DNS_PHAN_GIAI, 0, 0, 0},
        {OP_THU_KET_THUC, 12, 0, 0},
        {OP_BAT_LOI, 0, -1, 0},
        {OP_BIEN_SO, 77, 0, 0},
        {OP_IN, 0, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    const std::vector<Instruction> sumFunction = {
        {OP_PARAM, 0, 0, 0},
        {OP_BIEN_SO, 0, 0, 0},
        {OP_TEN_BIEN_ID, 0, 1, 0},
        {OP_GAN, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
        {OP_BIEN_SO, 0, 0, 0},
        {OP_LON_HON, 0, 0, 0},
        {OP_JUMP_IF_FALSE, 19, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 1, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
        {OP_CONG, 0, 0, 0},
        {OP_TEN_BIEN_ID, 0, 1, 0},
        {OP_GAN, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 0, 0},
        {OP_BIEN_SO, 1, 0, 0},
        {OP_TRU, 0, 0, 0},
        {OP_TEN_BIEN_ID, 0, 0, 0},
        {OP_GAN, 0, 0, 0},
        {OP_JUMP, 4, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 1, 0},
        {OP_TRA_VE, 0, 0, 0},
    };
    auto run = [&](bool jit, std::size_t &fast, std::size_t &slow) {
        VM vm(root, {"Việt"});
        vm.setFunctions({{7, sumFunction}});
        VMRuntimeFixture fixture(vm);
        std::string result;
        vm.setOutputSink([&](const std::string &value) { result += value; });
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        fast = vm.jitFastInstructionCount();
        slow = vm.jitInterpreterInstructionCount();
        return result;
    };
    std::size_t normalFast = 0, normalSlow = 0;
    std::size_t jitFast = 0, jitSlow = 0;
    const std::string expected = run(false, normalFast, normalSlow);
    const std::string actual = run(true, jitFast, jitSlow);
    expect(expected == actual && expected.find("10") != std::string::npos &&
               expected.find("77") != std::string::npos,
           "JIT giữ nguyên kết quả vòng lặp V++, gọi hàm, intrinsic và try/catch");
    expect(jitFast > 10 && jitSlow > 10 && normalFast == 0,
           "JIT thực sự chạy fast path bên trong hàm V++ và fallback từng opcode");
}

void testJitVariablesFloatAndRepeat() {
    // Variable initialization, global loads, implicit zero, variable IDs and
    // floating-point literals are used extensively by the .vi standard library.
    const std::vector<Instruction> code = {
        {OP_KHOI_TAO, 0, 2, 0},
        {OP_BIEN_SO, 7, 0, 0},
        {OP_TEN_BIEN_ID, 0, 2, 0},
        {OP_GAN, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 2, 0},
        {OP_IN, 0, 0, 0},
        {OP_BIEN_SO_FLOAT, 0, 0, 0},
        {OP_IN, 0, 0, 0},
        {OP_TEN_BIEN_GIA_TRI, 0, 9, 0},
        {OP_IN, 0, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    auto run = [&](bool jit, std::size_t &fast, std::size_t &slow) {
        VM vm(code, {"3.5"});
        VMRuntimeFixture fixture(vm);
        std::string output;
        vm.setOutputSink([&](const std::string &part) { output += part; });
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        fast = vm.jitFastInstructionCount();
        slow = vm.jitInterpreterInstructionCount();
        const std::string firstRun = output;
        vm.resetExecution();
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        expect(output == firstRun + firstRun,
               "chạy lại VM giữ output và không sử dụng JIT cache cũ");
        return firstRun;
    };
    std::size_t normalFast = 0, normalSlow = 0;
    std::size_t jitFast = 0, jitSlow = 0;
    const std::string normal = run(false, normalFast, normalSlow);
    const std::string compiled = run(true, jitFast, jitSlow);
    expect(normal == compiled && normal.find("3.5") != std::string::npos,
           "JIT bảo toàn biến toàn cục, mặc định zero và literal số thực");
    expect(jitFast >= 8 && jitSlow >= 1 && normalFast == 0,
           "JIT chạy trực tiếp opcode biến và số thực");

    const auto invalidFloat = [](bool jit) {
        VM vm({{OP_BIEN_SO_FLOAT, 0, 0, 0},
               {OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {"not-a-float"});
        VMRuntimeFixture fixture(vm);
        try {
            if (jit) fixture.runJitCompiledForTesting();
            else vm.run();
        } catch (const vietvm::runtime::RuntimeError &error) {
            return std::string(error.what());
        }
        return std::string();
    };
    const std::string error = invalidFloat(false);
    expect(!error.empty() && error == invalidFloat(true),
           "JIT giữ nguyên runtime diagnostic khi literal số thực không hợp lệ");
}

} // namespace

int main() {
    testVerificationCacheAndInvalidation();
    testModuleVerificationPrecedesSideEffects();
    testPeriodicGcKeepsCapacity();
    testLegacyIntrinsicUsesOpcodeDispatcher();
    testLegacyNetworkErrorCatchMatchesDirectOpcode();
    testJitFunctionsBranchesAndIntrinsics();
    testJitVariablesFloatAndRepeat();
    if (failures != 0) {
        std::cerr << failures << " runtime P0 hardening check(s) failed\n";
        return 1;
    }
    std::cout << "Runtime P0 hardening: PASS\n";
    return 0;
}
