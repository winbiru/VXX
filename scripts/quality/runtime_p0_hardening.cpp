#include <iostream>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "vpp/bytecode/foreign.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/foreign.h"
#include "vpp/runtime/vm.h"
#include "vpp/runtime/vm_fixture.h"
#include "common/vm_utils.h"

#if defined(VPP_TEST_POSIX_LIBFFI)
#include <cstdlib>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Exported by the test executable to verify the actual libffi ABI widths.
extern "C" std::int64_t vpp_ffi_test_i64(std::int64_t input) {
    return input;
}
extern "C" std::uint64_t vpp_ffi_test_u64(std::uint64_t input) {
    return input;
}
extern "C" std::uint32_t vpp_ffi_test_u32(std::uint32_t input) {
    return input;
}
extern "C" double vpp_ffi_test_f64(double input) {
    return input;
}
#endif

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
    bool dnsRejectedAsExpected = false;
    try {
        fixture.executeCall({OP_GOI, 1, -4, 0});
#if defined(_WIN32)
    } catch (const vietvm::runtime::LanguageException &) {
        dnsRejectedAsExpected = true;
#else
    } catch (const vietvm::runtime::RuntimeError &error) {
        dnsRejectedAsExpected =
            std::string(error.what()).find("legacy POSIX System opcode is disabled") !=
            std::string::npos ||
            std::string(error.what()).find("legacy VM System opcode is disabled") !=
            std::string::npos;
#endif
    }
    expect(dnsRejectedAsExpected,
           "OP_GOI DNS cũ tuân theo chính sách System của từng nền tảng");

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
    const auto runCatch = [](bool legacy, bool jit) {
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
        std::string runtimeError;
        try {
            if (jit) {
                VMRuntimeFixture fixture(vm);
                fixture.runJitCompiledForTesting();
            } else {
                vm.run(); // Also exercises verifier and VM exception unwinding.
            }
        } catch (const vietvm::runtime::RuntimeError &error) {
            runtimeError = error.what();
        }
        return std::make_pair(output, runtimeError);
    };
    const auto direct = runCatch(false, false);
    const auto legacy = runCatch(true, false);
    const auto directJit = runCatch(false, true);
    const auto legacyJit = runCatch(true, true);
#if defined(_WIN32)
    expect(!direct.first.empty() && direct.first == legacy.first &&
               direct.first == directJit.first && direct.first == legacyJit.first &&
               direct.first.find('1') != std::string::npos &&
               direct.second.empty() && legacy.second.empty() &&
               directJit.second.empty() && legacyJit.second.empty(),
           "Windows DNS lỗi bắt được nhất quán qua opcode/tên và interpreter/JIT");
#else
    expect(direct.first.empty() && legacy.first.empty() &&
               directJit.first.empty() && legacyJit.first.empty() &&
               !direct.second.empty() && direct.second == legacy.second &&
               direct.second == directJit.second && direct.second == legacyJit.second &&
               direct.second.find("legacy POSIX System opcode is disabled") !=
                   std::string::npos,
           "POSIX DNS opcode cũ bị chặn đồng nhất qua tên/opcode và interpreter/JIT");
#endif
}

void testRemovedEnvironmentOpcodeRejectInterpreterAndJit() {
    const auto *descriptor = vietvm::bytecode::intrinsicByOpcode(
        OP_VM_DOC_BIEN_MOI_TRUONG);
    expect(descriptor != nullptr && descriptor->arity == 1,
           "opcode môi trường cũ giữ descriptor cho diagnostic");
    if (descriptor == nullptr || descriptor->arity != 1) return;

    const std::vector<Instruction> code = {
        {OP_BIEN_SO, 0, 0, 0},
        {OP_VM_DOC_BIEN_MOI_TRUONG, 0, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    for (const bool jit : {false, true}) {
        VM vm(code, {});
        bool rejected = false;
        try {
            if (jit) {
                VMRuntimeFixture fixture(vm);
                fixture.runJitCompiledForTesting();
            } else {
                vm.run();
            }
        } catch (const vietvm::runtime::RuntimeError &error) {
            rejected = error.kind() ==
                vietvm::runtime::RuntimeErrorKind::CallBoundary &&
                std::string(error.what()).find(
                    "legacy environment opcode is disabled") != std::string::npos;
        }
        expect(rejected, "opcode môi trường cũ bị chặn trên mọi OS/JIT");
    }
}

#if !defined(_WIN32)
void testRemovedPosixSystemOpcodesRejectInterpreterAndJit() {
    // All removed OS intrinsics must fail before reaching the Windows-only
    // legacy helpers, irrespective of the strict diagnostic testing flag.
    constexpr std::array<Opcode, 29> removed = {{
        OP_VM_DONG_HO_DIA_PHUONG,
        OP_VM_DONG_HO_UTC,
        OP_VM_DUONG_DAN_TON_TAI,
        OP_VM_LA_TEP,
        OP_VM_LA_THU_MUC,
        OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET,
        OP_VM_LIET_KE_THU_MUC,
        OP_VM_TAO_THU_MUC,
        OP_VM_XOA_DUONG_DAN,
        OP_VM_NGAU_NHIEN_BAO_MAT_BYTES,
        OP_VM_DNS_PHAN_GIAI,
        OP_VM_SOCKET_PHAN_GIAI,
        OP_VM_SOCKET_CHAP_NHAN,
        OP_VM_SOCKET_DAT_TIMEOUT,
        OP_VM_SOCKET_DONG,
        OP_VM_SOCKET_GUI,
        OP_VM_SOCKET_NHAN,
        OP_VM_SOCKET_TCP_LANG_NGHE,
        OP_VM_SOCKET_TCP_MO,
        OP_VM_SOCKET_UDP_MO,
        OP_VM_SOCKET_TLS_NANG_CAP,
        OP_VM_TIEN_TRINH_CHAY,
        OP_VM_NGU_MILI_GIAY,
        OP_VM_THOI_GIAN_DON_DIEU_MS,
        OP_VM_IO_DOC_BYTES,
        OP_VM_IO_DOC_FILE,
        OP_VM_IO_GHI_BYTES,
        OP_VM_IO_GHI_FILE,
        OP_VM_IO_GHI_TIEP_FILE,
    }};

    for (const Opcode opcode : removed) {
        const auto *descriptor = vietvm::bytecode::intrinsicByOpcode(opcode);
        expect(descriptor != nullptr, "opcode System cũ có descriptor");
        if (descriptor == nullptr) continue;

        std::vector<Instruction> code;
        for (std::size_t i = 0; i < descriptor->arity; ++i) {
            code.push_back({OP_BIEN_SO, 0, 0, 0});
        }
        code.push_back({opcode, 0, 0, 0});
        code.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});

        const auto run = [&](bool jit) {
            VM vm(code, {});
            try {
                if (jit) {
                    VMRuntimeFixture fixture(vm);
                    fixture.runJitCompiledForTesting();
                } else {
                    vm.run();
                }
            } catch (const vietvm::runtime::RuntimeError &error) {
                return std::string(error.what());
            }
            return std::string();
        };

        const std::string interpretedError = run(false);
        const std::string jitError = run(true);
        const bool fileOpcode =
            opcode == OP_VM_IO_DOC_BYTES || opcode == OP_VM_IO_DOC_FILE ||
            opcode == OP_VM_IO_GHI_BYTES || opcode == OP_VM_IO_GHI_FILE ||
            opcode == OP_VM_IO_GHI_TIEP_FILE;
        const bool entropyOpcode = opcode == OP_VM_NGAU_NHIEN_BAO_MAT_BYTES;
        const bool monotonicOpcode = opcode == OP_VM_THOI_GIAN_DON_DIEU_MS;
        const bool sleepOpcode = opcode == OP_VM_NGU_MILI_GIAY;
        const std::string diagnostic = entropyOpcode
            ? "legacy secure entropy opcode is disabled"
            : monotonicOpcode ? "legacy monotonic clock opcode is disabled"
            : sleepOpcode ? "legacy sleep opcode is disabled"
            : fileOpcode ? "legacy VM file opcode is disabled" : "";
        const bool disabledDiagnostic = diagnostic.empty()
            ? interpretedError.find("legacy POSIX System opcode is disabled") !=
                  std::string::npos ||
              interpretedError.find("legacy VM System opcode is disabled") !=
                  std::string::npos
            : interpretedError.find(diagnostic) != std::string::npos;
        expect(disabledDiagnostic && interpretedError == jitError,
               "POSIX opcode System cũ bị chặn trước side effect, interpreter/JIT: " +
                   std::string(descriptor->name));
    }
}
#endif

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
        {OP_VM_STRING_FROM_BYTES, 0, 0, 0},
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

vietvm::bytecode::ForeignFunctionDescriptor getenvDescriptor() {
    vietvm::bytecode::ForeignFunctionDescriptor descriptor;
    descriptor.id = 0;
    descriptor.library = "system.c";
    descriptor.symbol = "getenv";
    descriptor.parameters = {vietvm::bytecode::ForeignAbiType::CString};
    descriptor.result = vietvm::bytecode::ForeignAbiType::CString;
    descriptor.abi = "c";
    descriptor.capability = "system.env.read";
    return descriptor;
}

void testForeignVerificationPrecedesSideEffects() {
    for (const bool jit : {false, true}) {
        VM vm({{OP_CHUOI, 0, 0, 0},
               {OP_IN, 0, 0, 0},
               {OP_FFI_CALL, 0, 0, 0},
               {OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {"should-not-print"});
        vm.setForeignFunctions({getenvDescriptor()});
        vm.setForeignCapabilities({"system.env.read"});
        std::string output;
        vm.setOutputSink([&](const std::string &part) { output += part; });
        bool verifierRejected = false;
        try {
            if (jit) {
                VMRuntimeFixture fixture(vm);
                fixture.runJitCompiledForTesting();
            } else {
                vm.run();
            }
        } catch (const vietvm::runtime::RuntimeError &error) {
            verifierRejected = error.kind() ==
                vietvm::runtime::RuntimeErrorKind::VmFault &&
                std::string(error.what()).find("số đối số OP_FFI_CALL") !=
                    std::string::npos;
        }
        expect(verifierRejected && output.empty(),
               "verifier chặn arity FFI hỏng trước side effect trên interpreter/JIT");
    }
}

void testForeignFfiCapabilityAndRuntime() {
    const std::vector<Instruction> code = {
        {OP_CHUOI, 0, 0, 0},
        {OP_FFI_CALL, 1, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };

    VM denied(code, {"PATH"});
    denied.setForeignFunctions({getenvDescriptor()});
    bool deniedByCapability = false;
    try {
        denied.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        deniedByCapability =
            std::string(error.what()).find("thiếu khả năng") != std::string::npos;
    }
    expect(deniedByCapability,
           "FFI từ chối getenv khi host chưa cấp system.env.read");

    auto substituted = getenvDescriptor();
    substituted.symbol = "system";
    substituted.result = vietvm::bytecode::ForeignAbiType::I32;
    VM confusedDeputy(code, {"true"});
    confusedDeputy.setForeignFunctions({substituted});
    confusedDeputy.setForeignCapabilities({"system.env.read"});
    bool blockedUnrelatedSymbol = false;
    try {
        confusedDeputy.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        blockedUnrelatedSymbol =
            std::string(error.what()).find("chỉ cho phép getenv") != std::string::npos;
    }
    expect(blockedUnrelatedSymbol,
           "quyền system.env.read không gọi được symbol system của libc");

    auto incompatibleSignature = getenvDescriptor();
    incompatibleSignature.result = vietvm::bytecode::ForeignAbiType::I32;
    VM invalidSignature(code, {"PATH"});
    invalidSignature.setForeignFunctions({incompatibleSignature});
    invalidSignature.setForeignCapabilities({"system.env.read"});
    bool blockedSignature = false;
    try {
        invalidSignature.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        blockedSignature =
            std::string(error.what()).find("chỉ cho phép getenv") != std::string::npos;
    }
    expect(blockedSignature,
           "quyền system.env.read khóa cả signature ABI của getenv");

    VM embeddedNul(code, {std::string("PATH\0X", 6)});
    embeddedNul.setForeignFunctions({getenvDescriptor()});
    embeddedNul.setForeignCapabilities({"system.env.read"});
    bool blockedEmbeddedNul = false;
    try {
        embeddedNul.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        blockedEmbeddedNul = std::string(error.what()).find("byte NUL") != std::string::npos;
    }
    expect(blockedEmbeddedNul,
           "FFI không âm thầm cắt c_chuỗi tại NUL bên trong");

    VM existing(code, {"PATH"});
    existing.setForeignFunctions({getenvDescriptor()});
    existing.setForeignCapabilities({"system.env.read"});
    VMRuntimeFixture existingFixture(existing);
    existing.run();
    expect(std::holds_alternative<std::string>(existingFixture.top()) &&
               !std::get<std::string>(existingFixture.top()).empty(),
           "getenv qua FFI trả chuỗi cho biến PATH hiện hữu");

    VM missing(code, {"__VPP_FFI_TEST_ENV_DOES_NOT_EXIST_7D4B7E13__"});
    missing.setForeignFunctions({getenvDescriptor()});
    missing.setForeignCapabilities({"system.env.read"});
    VMRuntimeFixture missingFixture(missing);
    missing.run();
    expect(std::holds_alternative<std::monostate>(missingFixture.top()),
           "getenv qua FFI ánh xạ nullptr thành rỗng V++");

    const std::vector<Instruction> outputCode = {
        {OP_CHUOI, 0, 0, 0},
        {OP_FFI_CALL, 1, 0, 0},
        {OP_IN, 0, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    const auto run = [&](bool jit, std::size_t &fast, std::size_t &slow) {
        VM vm(outputCode, {"PATH"});
        vm.setForeignFunctions({getenvDescriptor()});
        vm.setForeignCapabilities({"system.env.read"});
        VMRuntimeFixture fixture(vm);
        std::string output;
        vm.setOutputSink([&](const std::string &part) { output += part; });
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        fast = vm.jitFastInstructionCount();
        slow = vm.jitInterpreterInstructionCount();
        return output;
    };
    std::size_t normalFast = 0, normalSlow = 0;
    std::size_t jitFast = 0, jitSlow = 0;
    const std::string normal = run(false, normalFast, normalSlow);
    const std::string compiled = run(true, jitFast, jitSlow);
    expect(!normal.empty() && normal == compiled,
           "JIT giữ nguyên kết quả OP_FFI_CALL");
    expect(jitSlow >= 1 && normalFast == 0,
           "JIT fallback OP_FFI_CALL qua interpreter dispatcher");
}

void testForeignProcessIdFfi() {
    vietvm::bytecode::ForeignFunctionDescriptor descriptor;
    descriptor.id = 0;
    descriptor.library = "system.c";
    descriptor.symbol = "getpid";
    descriptor.result = vietvm::bytecode::ForeignAbiType::I32;
    descriptor.abi = "c";
    descriptor.capability = "system.process.id";
    const std::vector<Instruction> code = {
        {OP_FFI_CALL, 0, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };

    VM denied(code, {});
    denied.setForeignFunctions({descriptor});
    bool missingGrant = false;
    try {
        denied.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        missingGrant = std::string(error.what()).find("thiếu khả năng") != std::string::npos;
    }
    expect(missingGrant, "getpid FFI cần host cấp system.process.id");

    auto substituted = descriptor;
    substituted.symbol = "getppid";
    VM wrongSymbol(code, {});
    wrongSymbol.setForeignFunctions({substituted});
    wrongSymbol.setForeignCapabilities({"system.process.id"});
    bool rejectedSymbol = false;
    try {
        wrongSymbol.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        rejectedSymbol = std::string(error.what()).find("chỉ cho phép getpid") != std::string::npos;
    }
    expect(rejectedSymbol, "system.process.id không thể gọi symbol khác");

    auto changedResult = descriptor;
    changedResult.result = vietvm::bytecode::ForeignAbiType::CString;
    VM wrongAbi(code, {});
    wrongAbi.setForeignFunctions({changedResult});
    wrongAbi.setForeignCapabilities({"system.process.id"});
    bool rejectedAbi = false;
    try {
        wrongAbi.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        rejectedAbi = std::string(error.what()).find("chỉ cho phép getpid") != std::string::npos;
    }
    expect(rejectedAbi, "system.process.id khóa chữ ký trả về i32");

    auto changedLibrary = descriptor;
    changedLibrary.library = "libexample.so";
    VM wrongLibrary(code, {});
    wrongLibrary.setForeignFunctions({changedLibrary});
    wrongLibrary.setForeignCapabilities({"system.process.id"});
    bool rejectedLibrary = false;
    try {
        wrongLibrary.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        rejectedLibrary = std::string(error.what()).find("chỉ cho phép getpid") != std::string::npos;
    }
    expect(rejectedLibrary, "system.process.id khóa logical library system.c");

#if defined(VPP_TEST_POSIX_LIBFFI)
    const auto call = [&](bool jit) {
        VM vm(code, {});
        vm.setForeignFunctions({descriptor});
        vm.setForeignCapabilities({"system.process.id"});
        VMRuntimeFixture fixture(vm);
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        return fixture.top();
    };
    const auto interpreted = call(false);
    const auto compiled = call(true);
    expect(std::holds_alternative<int>(interpreted) &&
               std::get<int>(interpreted) == static_cast<int>(::getpid()),
           "getpid FFI không đối số trả PID thật của host");
    expect(interpreted == compiled,
           "getpid FFI giữ kết quả khi chạy qua JIT fallback");
#endif
}

void testForeignParentProcessIdFfi() {
    using vietvm::bytecode::ForeignAbiType;
    vietvm::bytecode::ForeignFunctionDescriptor descriptor;
    descriptor.id = 0;
    descriptor.library = "system.c";
    descriptor.symbol = "getppid";
    descriptor.result = ForeignAbiType::I32;
    descriptor.abi = "c";
    descriptor.capability = "system.process.parent_id";
    const std::vector<Instruction> code = {
        {OP_FFI_CALL, 0, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    const auto failure = [&](const auto &binding,
                             std::vector<std::string> grants) {
        const std::vector<Instruction> callCode =
            binding.parameters.empty()
                ? code
                : std::vector<Instruction>{
                    {OP_BIEN_SO, 1, 0, 0},
                    {OP_FFI_CALL, 1, 0, 0},
                    {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
                };
        VM vm(callCode, {});
        vm.setForeignFunctions({binding});
        vm.setForeignCapabilities(
            std::unordered_set<std::string>(grants.begin(), grants.end()));
        try {
            vm.run();
        } catch (const vietvm::runtime::RuntimeError &error) {
            return std::string(error.what());
        }
        return std::string();
    };
    expect(failure(descriptor, {}).find("thiếu khả năng") != std::string::npos,
           "getppid cần quyền system.process.parent_id");
    expect(failure(descriptor, {"system.process.id"}).find("thiếu khả năng") !=
               std::string::npos,
           "quyền system.process.id không mở khóa getppid");

    auto wrongSymbol = descriptor;
    wrongSymbol.symbol = "getpid";
    expect(failure(wrongSymbol, {"system.process.parent_id"}).find(
               "chỉ cho phép getppid") != std::string::npos,
           "quyền parent_id không thể tráo thành getpid");
    auto wrongLibrary = descriptor;
    wrongLibrary.library = "libexample.so";
    expect(failure(wrongLibrary, {"system.process.parent_id"}).find(
               "chỉ cho phép getppid") != std::string::npos,
           "quyền parent_id khóa thư viện system.c");
    auto wrongParameters = descriptor;
    wrongParameters.parameters = {ForeignAbiType::I32};
    expect(failure(wrongParameters, {"system.process.parent_id"}).find(
               "chỉ cho phép getppid") != std::string::npos,
           "quyền parent_id khóa số lượng tham số");
    auto wrongResult = descriptor;
    wrongResult.result = ForeignAbiType::U32;
    expect(failure(wrongResult, {"system.process.parent_id"}).find(
               "chỉ cho phép getppid") != std::string::npos,
           "quyền parent_id khóa kiểu trả về");
    auto wrongAbi = descriptor;
    wrongAbi.abi = "stdcall";
    expect(failure(wrongAbi, {"system.process.parent_id"}).find(
               "foreign descriptor không hợp lệ") != std::string::npos,
           "quyền parent_id khóa ABI c");

#if defined(VPP_TEST_POSIX_LIBFFI)
    const auto call = [&](bool jit) {
        VM vm(code, {});
        vm.setForeignFunctions({descriptor});
        vm.setForeignCapabilities({"system.process.parent_id"});
        VMRuntimeFixture fixture(vm);
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        return fixture.top();
    };
    const auto interpreted = call(false);
    const auto compiled = call(true);
    expect(std::holds_alternative<int>(interpreted) &&
               std::get<int>(interpreted) == static_cast<int>(::getppid()),
           "getppid FFI trả PID của tiến trình cha thật");
    expect(interpreted == compiled,
           "getppid FFI giữ kết quả trong JIT fallback");
#endif
}

void testForeignSleepFfi() {
    using vietvm::bytecode::ForeignAbiType;
    vietvm::bytecode::ForeignFunctionDescriptor descriptor;
    descriptor.id = 0;
    descriptor.library = "system.c";
    descriptor.symbol = "usleep";
    descriptor.parameters = {ForeignAbiType::U32};
    descriptor.result = ForeignAbiType::I32;
    descriptor.abi = "c";
    descriptor.capability = "system.time.sleep";
    const std::vector<Instruction> code = {
        {OP_BIEN_SO, 1000, 0, 0},
        {OP_FFI_CALL, 1, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    const auto failure = [&](const auto &binding, bool grant) {
        VM vm(code, {});
        vm.setForeignFunctions({binding});
        if (grant) vm.setForeignCapabilities({"system.time.sleep"});
        try {
            vm.run();
        } catch (const vietvm::runtime::RuntimeError &error) {
            return std::string(error.what());
        }
        return std::string();
    };
    expect(failure(descriptor, false).find("thiếu khả năng") != std::string::npos,
           "usleep FFI yêu cầu quyền system.time.sleep");

    auto wrongSymbol = descriptor;
    wrongSymbol.symbol = "system";
    expect(failure(wrongSymbol, true).find("chỉ cho phép usleep") != std::string::npos,
           "system.time.sleep không thể gọi symbol khác");
    auto wrongLibrary = descriptor;
    wrongLibrary.library = "libother.dylib";
    expect(failure(wrongLibrary, true).find("chỉ cho phép usleep") != std::string::npos,
           "system.time.sleep khóa logical library");
    auto wrongParameters = descriptor;
    wrongParameters.parameters = {ForeignAbiType::I32};
    expect(failure(wrongParameters, true).find("chỉ cho phép usleep") != std::string::npos,
           "system.time.sleep khóa ABI tham số u32");
    auto wrongResult = descriptor;
    wrongResult.result = ForeignAbiType::Void;
    expect(failure(wrongResult, true).find("chỉ cho phép usleep") != std::string::npos,
           "system.time.sleep khóa ABI trả về i32");
    auto wrongAbi = descriptor;
    wrongAbi.abi = "stdcall";
    expect(failure(wrongAbi, true).find("foreign descriptor không hợp lệ") != std::string::npos,
           "system.time.sleep khóa calling convention c");

#if defined(VPP_TEST_POSIX_LIBFFI)
    const auto call = [&](bool jit) {
        VM vm(code, {});
        vm.setForeignFunctions({descriptor});
        vm.setForeignCapabilities({"system.time.sleep"});
        VMRuntimeFixture fixture(vm);
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        return fixture.top();
    };
    expect(call(false) == make_int_value(0),
           "usleep FFI trả mã thành công trên POSIX");
    expect(call(true) == make_int_value(0),
           "usleep FFI chạy được qua JIT fallback");

    VM negative({{OP_BIEN_SO, -1, 0, 0},
                 {OP_FFI_CALL, 1, 0, 0},
                 {OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    negative.setForeignFunctions({descriptor});
    negative.setForeignCapabilities({"system.time.sleep"});
    bool invalidArgument = false;
    try {
        negative.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        invalidArgument = std::string(error.what()).find("u32 yêu cầu") != std::string::npos;
    }
    expect(invalidArgument,
           "usleep FFI không chuyển số âm thành thời lượng u32 rất lớn");
#endif
}

#if defined(VPP_TEST_POSIX_LIBFFI)
void testForeignPosixErrorSnapshot() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;

    ForeignFunctionDescriptor closeDescriptor;
    closeDescriptor.id = 0;
    closeDescriptor.library = "system.c";
    closeDescriptor.symbol = "close";
    closeDescriptor.parameters = {ForeignAbiType::I32};
    closeDescriptor.result = ForeignAbiType::I32;
    closeDescriptor.abi = "c";
    closeDescriptor.capability = "test.ffi.close";

    ForeignFunctionDescriptor errnoDescriptor;
    errnoDescriptor.id = 1;
    errnoDescriptor.library = "system.ffi";
    errnoDescriptor.symbol = "last_errno";
    errnoDescriptor.result = ForeignAbiType::I32;
    errnoDescriptor.abi = "c";
    errnoDescriptor.capability = "system.ffi.error";

    auto eintrDescriptor = errnoDescriptor;
    eintrDescriptor.id = 2;
    eintrDescriptor.symbol = "eintr";

    ForeignFunctionDescriptor pidDescriptor;
    pidDescriptor.id = 3;
    pidDescriptor.library = "system.c";
    pidDescriptor.symbol = "getpid";
    pidDescriptor.result = ForeignAbiType::I32;
    pidDescriptor.abi = "c";
    pidDescriptor.capability = "system.process.id";

    const std::vector<ForeignFunctionDescriptor> descriptors = {
        closeDescriptor, errnoDescriptor, eintrDescriptor, pidDescriptor};
    const std::unordered_set<std::string> grants = {
        "test.ffi.close", "system.ffi.error", "system.process.id"};

    const auto invoke = [&](const std::vector<Instruction> &code, bool jit) {
        VM vm(code, {});
        vm.setForeignFunctions(descriptors);
        vm.setForeignCapabilities(grants);
        VMRuntimeFixture fixture(vm);
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        return fixture.top();
    };

    const std::vector<Instruction> failedClose = {
        {OP_BIEN_SO, -1, 0, 0},
        {OP_FFI_CALL, 1, 0, 0},
        {OP_FFI_CALL, 0, 2, 0}, // Query host EINTR; must not overwrite errno.
        {OP_FFI_CALL, 0, 1, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    expect(invoke(failedClose, false) == make_int_value(EBADF),
           "FFI chụp errno=EBADF ngay sau close(-1) trong interpreter");
    expect(invoke(failedClose, true) == make_int_value(EBADF),
           "FFI lưu nguyên errno=EBADF qua JIT và truy vấn eintr");

    const std::vector<Instruction> succeededPid = {
        {OP_FFI_CALL, 0, 3, 0},
        {OP_FFI_CALL, 0, 1, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    expect(invoke(succeededPid, false) == make_int_value(0),
           "getpid thành công xóa errno cũ trước native call");
    expect(invoke(succeededPid, true) == make_int_value(0),
           "JIT giữ errno=0 sau getpid thành công");

    const std::vector<Instruction> queryEintr = {
        {OP_FFI_CALL, 0, 2, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    expect(invoke(queryEintr, false) == make_int_value(EINTR),
           "system.ffi.eintr trả về hằng số EINTR của host");

    const std::vector<Instruction> onlyErrno = {
        {OP_FFI_CALL, 0, 1, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    expect(invoke(onlyErrno, false) == make_int_value(0),
           "errno là trạng thái riêng từng VM, không rò từ lần chạy trước");

    VM reused(failedClose, {});
    reused.setForeignFunctions(descriptors);
    reused.setForeignCapabilities(grants);
    VMRuntimeFixture reusedFixture(reused);
    reused.run();
    expect(reusedFixture.top() == make_int_value(EBADF),
           "VM ghi nhận lỗi native trước khi reset");
    reused.resetExecution();
    reusedFixture.executeForeignCall({OP_FFI_CALL, 0, 1, 0});
    expect(reusedFixture.top() == make_int_value(0),
           "resetExecution xóa snapshot errno");
    reusedFixture.resetExecutionForBenchmark();
    reusedFixture.push(make_int_value(-1));
    reusedFixture.executeForeignCall({OP_FFI_CALL, 1, 0, 0});
    reusedFixture.resetExecutionForBenchmark();
    reusedFixture.executeForeignCall({OP_FFI_CALL, 0, 1, 0});
    expect(reusedFixture.top() == make_int_value(0),
           "reset benchmark không giữ lại errno từ lời gọi trước");

    const auto failure = [&](ForeignFunctionDescriptor binding,
                             bool grant) {
        binding.id = 0;
        std::vector<Instruction> callCode;
        for (std::size_t index = 0; index < binding.parameters.size(); ++index) {
            callCode.push_back({OP_BIEN_SO, 1, 0, 0});
        }
        callCode.push_back({OP_FFI_CALL,
                            static_cast<int>(binding.parameters.size()), 0, 0});
        callCode.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
        VM vm(callCode, {});
        vm.setForeignFunctions({binding});
        if (grant) vm.setForeignCapabilities({"system.ffi.error"});
        try {
            vm.run();
        } catch (const vietvm::runtime::RuntimeError &error) {
            return std::string(error.what());
        }
        return std::string();
    };
    expect(failure(errnoDescriptor, false).find("thiếu khả năng") !=
               std::string::npos,
           "đọc errno cần quyền system.ffi.error");
    auto wrongSymbol = errnoDescriptor;
    wrongSymbol.symbol = "system";
    expect(failure(wrongSymbol, true).find("chỉ cho phép") !=
               std::string::npos,
           "quyền đọc errno không thể tráo thành symbol libc khác");
    auto wrongLibrary = errnoDescriptor;
    wrongLibrary.library = "system.c";
    expect(failure(wrongLibrary, true).find("chỉ cho phép") !=
               std::string::npos,
           "quyền đọc errno không cho đổi logical library");
    auto wrongAbi = errnoDescriptor;
    wrongAbi.abi = "stdcall";
    expect(!failure(wrongAbi, true).empty(),
           "quyền đọc errno chỉ hỗ trợ ABI c");
    auto wrongParameters = errnoDescriptor;
    wrongParameters.parameters = {ForeignAbiType::I32};
    expect(failure(wrongParameters, true).find("chỉ cho phép") !=
               std::string::npos,
           "quyền đọc errno khóa arity 0");
}
#endif

#if defined(VPP_TEST_POSIX_LIBFFI)
void testForeignExact64BitIntegers() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::AbiInteger;

    const std::unordered_set<std::string> grants = {"test.ffi.int64"};
    ForeignFunctionDescriptor signedBinding;
    signedBinding.id = 0;
    signedBinding.library = "system.c";
    signedBinding.symbol = "vpp_ffi_test_i64";
    signedBinding.parameters = {ForeignAbiType::I64};
    signedBinding.result = ForeignAbiType::I64;
    signedBinding.capability = "test.ffi.int64";
    ForeignFunctionDescriptor unsignedBinding = signedBinding;
    unsignedBinding.symbol = "vpp_ffi_test_u64";
    unsignedBinding.parameters = {ForeignAbiType::U64};
    unsignedBinding.result = ForeignAbiType::U64;
    ForeignFunctionDescriptor unsigned32Binding = signedBinding;
    unsigned32Binding.symbol = "vpp_ffi_test_u32";
    unsigned32Binding.parameters = {ForeignAbiType::U32};
    unsigned32Binding.result = ForeignAbiType::U32;

    auto call = [&](const ForeignFunctionDescriptor &binding,
                    const StackValue &value) {
        return executeForeignCall(binding, {value}, grants, 0).value;
    };
    const StackValue i64Min = call(signedBinding,
        make_string_value("-9223372036854775808"));
    const StackValue i64Max = call(signedBinding,
        make_string_value("9223372036854775807"));
    const StackValue u64Max = call(unsignedBinding,
        make_string_value("18446744073709551615"));
    expect(sv_to_string(i64Min) == "-9223372036854775808" &&
           sv_to_string(i64Max) == "9223372036854775807" &&
           sv_to_string(u64Max) == "18446744073709551615",
           "i64/u64 FFI roundtrip giữ nguyên đầy đủ 64 bit");
    const StackValue u32Max = call(unsigned32Binding,
        make_string_value("4294967295"));
    expect(std::holds_alternative<AbiInteger>(u32Max) &&
           sv_to_string(u32Max) == "4294967295" &&
           sameStackValue(call(unsigned32Binding, u32Max), u32Max) &&
           std::holds_alternative<int>(
               call(unsigned32Binding, make_int_value(42))),
           "u32 FFI roundtrip đủ UINT32_MAX và giữ tương thích int32 nhỏ");
    expect(std::holds_alternative<AbiInteger>(u64Max) &&
           !std::get<AbiInteger>(u64Max).isSigned &&
           compareAbiIntegers(asAbiInteger(i64Max), asAbiInteger(u64Max)) < 0 &&
           compareAbiIntegers(asAbiInteger(i64Min), asAbiInteger(u64Max)) < 0,
           "boxed ABI int giữ signedness và so sánh signed/unsigned chính xác");
    expect(sameStackValue(call(signedBinding, make_int_value(12)),
                          make_int_value(12)) &&
           sameStackValue(call(unsignedBinding, make_int_value(12)),
                          make_int_value(12)) &&
           !sameStackValue(u64Max, make_float_value(18446744073709551616.0)),
           "equality i64/u64 so với int32 chính xác, không làm tròn qua double");
    expect(sv_to_string(call(signedBinding, i64Min)) == sv_to_string(i64Min) &&
           sv_to_string(call(unsignedBinding, u64Max)) == sv_to_string(u64Max),
           "giá trị boxed đi qua nhiều foreign call không mất bit");

    auto rejects = [&](const ForeignFunctionDescriptor &binding,
                       const StackValue &value) {
        try {
            (void)call(binding, value);
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
        return false;
    };
    expect(rejects(signedBinding, make_string_value("9223372036854775808")) &&
           rejects(signedBinding, make_string_value("-9223372036854775809")) &&
           rejects(signedBinding, make_string_value("2.5")) &&
           rejects(signedBinding, u64Max) &&
           rejects(unsignedBinding, make_int_value(-1)) &&
           rejects(unsignedBinding, i64Min) &&
           rejects(unsignedBinding, make_string_value("18446744073709551616")) &&
           rejects(unsignedBinding, make_string_value(" 1")) &&
           rejects(unsignedBinding, make_float_value(42.0)),
           "marshaller từ chối overflow, số âm, float và chuỗi sai định dạng");
    expect(rejects(unsigned32Binding, make_string_value("4294967296")) &&
           rejects(unsigned32Binding, make_string_value("-1")) &&
           rejects(unsigned32Binding, make_int_value(-1)) &&
           rejects(unsigned32Binding, u64Max) &&
           rejects(unsigned32Binding, make_float_value(42.0)),
           "u32 marshaller chặn tràn uint32, số âm và chuyển đổi float");

    ForeignFunctionDescriptor floatBinding = signedBinding;
    floatBinding.symbol = "vpp_ffi_test_f64";
    floatBinding.parameters = {ForeignAbiType::F64};
    floatBinding.result = ForeignAbiType::F64;
    expect(sv_to_string(call(floatBinding, make_float_value(1.25))) == "1.25" &&
           sv_to_string(call(floatBinding,
               make_abi_integer_value(AbiInteger::unsignedNumber(1ULL << 53)))) ==
               "9007199254740992.0",
           "f64 FFI nhận double và boxed integer trong miền chính xác");
    bool precisionRejectedAtFfiBoundary = false;
    try {
        (void)call(floatBinding,
            make_abi_integer_value(AbiInteger::unsignedNumber((1ULL << 53) + 1)));
    } catch (const vietvm::runtime::RuntimeError &error) {
        const std::string message = error.what();
        precisionRejectedAtFfiBoundary =
            message.find("FFI:") != std::string::npos &&
            message.find("độ chính xác f64") != std::string::npos;
    }
    expect(precisionRejectedAtFfiBoundary,
           "marshaller f64 báo lỗi FFI khi boxed u64 làm tròn ngoài 2^53");

    const auto boxedComparison = evaluateBinaryOperator(
        OP_LON_HON, u64Max, i64Max, 0);
    expect(std::holds_alternative<int>(boxedComparison) &&
           std::get<int>(boxedComparison) == 1 &&
           !stackValueTruthy(call(unsignedBinding, make_int_value(0))),
           "V++ so sánh ABI integer chính xác và đánh giá truthiness của 0");
    const auto arithmetic = [&](int op, const StackValue &left,
                                const StackValue &right) -> StackValue {
        if (op == OP_MODULO) return evaluateModuloOperator(left, right, op, 0);
        return evaluateBinaryOperator(op, left, right, 0);
    };
    const auto resultEquals = [&](int op, const StackValue &left,
                                  const StackValue &right,
                                  const std::string &expected) {
        return sv_to_string(arithmetic(op, left, right)) == expected;
    };
    const auto arithmeticRejects = [&](int op, const StackValue &left,
                                       const StackValue &right) {
        try {
            (void)arithmetic(op, left, right);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };
    expect(resultEquals(OP_CONG, i64Min, i64Max, "-1") &&
           resultEquals(OP_TRU, i64Max, make_int_value(1), "9223372036854775806") &&
           resultEquals(OP_NHAN, i64Min, make_int_value(1), "-9223372036854775808") &&
           resultEquals(OP_CHIA, i64Min, make_int_value(2), "-4611686018427387904") &&
           resultEquals(OP_MODULO, i64Min, make_int_value(3), "-2"),
           "số học i64 không mất bit trên cộng/trừ/nhân/chia/chia dư");
    expect(resultEquals(OP_TRU, u64Max, make_int_value(1), "18446744073709551614") &&
           resultEquals(OP_CONG, u64Max, make_int_value(0), "18446744073709551615") &&
           resultEquals(OP_NHAN, u64Max, make_int_value(1), "18446744073709551615") &&
           resultEquals(OP_CHIA, u64Max, make_int_value(2), "9223372036854775807") &&
           resultEquals(OP_MODULO, u64Max, make_int_value(2), "1"),
           "số học u64 bảo toàn UINT64_MAX và phần dư");
    expect(arithmeticRejects(OP_CONG, u64Max, make_int_value(1)) &&
           arithmeticRejects(OP_TRU, make_int_value(0), u64Max) &&
           arithmeticRejects(OP_NHAN, u64Max, make_int_value(2)) &&
           arithmeticRejects(OP_CONG, i64Max, make_int_value(1)) &&
           arithmeticRejects(OP_TRU, i64Max, i64Min) &&
           arithmeticRejects(OP_TRU, i64Min, make_int_value(1)) &&
           arithmeticRejects(OP_NHAN, i64Min, make_int_value(-1)) &&
           arithmeticRejects(OP_CHIA, i64Min, make_int_value(-1)) &&
           arithmeticRejects(OP_MODULO, i64Min, make_int_value(-1)) &&
           arithmeticRejects(OP_CHIA, u64Max, make_int_value(0)) &&
           arithmeticRejects(OP_MODULO, u64Max, make_int_value(0)) &&
           arithmeticRejects(OP_CONG, u64Max, make_int_value(-1)) &&
           arithmeticRejects(OP_CONG, u64Max, make_float_value(1.0)),
           "số học ABI 64-bit từ chối overflow, 0 và mixed-sign/float");

    const auto run = [&](bool jit) {
        VM vm({{OP_CHUOI, 0, 0, 0}, {OP_FFI_CALL, 1, 0, 0},
               {OP_IN, 0, 0, 0}, {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
              {"18446744073709551615"});
        vm.setForeignFunctions({unsignedBinding});
        vm.setForeignCapabilities(grants);
        VMRuntimeFixture fixture(vm);
        std::string output;
        vm.setOutputSink([&](const std::string &part) { output += part; });
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        return output;
    };
    expect(run(false) == run(true) &&
           run(false).find("18446744073709551615") != std::string::npos,
           "i64/u64 FFI descriptor chạy và hiển thị đúng trên interpreter/JIT");
    const auto runArithmetic = [&](bool jit) {
        VM vm({{OP_CHUOI, 0, 0, 0}, {OP_FFI_CALL, 1, 0, 0},
               {OP_DUNG_GIA_TRI, 0, 0, 0}, {OP_TRU, 0, 0, 0},
               {OP_IN, 0, 0, 0}, {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
              {"18446744073709551615"});
        vm.setForeignFunctions({unsignedBinding});
        vm.setForeignCapabilities(grants);
        VMRuntimeFixture fixture(vm);
        std::string output;
        vm.setOutputSink([&](const std::string &part) { output += part; });
        if (jit) fixture.runJitCompiledForTesting();
        else vm.run();
        return output;
    };
    expect(runArithmetic(false) == runArithmetic(true) &&
           runArithmetic(false).find("18446744073709551614") != std::string::npos,
           "FFI u64 đi qua toán tử VM trên interpreter và JIT đúng bit");
}
#endif

void testForeignMonotonicClockBuffer() {
#if defined(VPP_TEST_POSIX_LIBFFI)
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::runtime::executeForeignCall;
    vietvm::bytecode::ForeignFunctionDescriptor descriptor;
    descriptor.library = "system.c";
    descriptor.symbol = "clock_gettime";
    descriptor.abi = "c";
    descriptor.parameters = {ForeignAbiType::I32, ForeignAbiType::BufferOut};
    descriptor.result = ForeignAbiType::I32;
    descriptor.capability = "system.time.monotonic";
    const std::unordered_set<std::string> caps = {"system.time.monotonic"};
    auto bytes = vietvm::runtime::make_list_value(
        std::vector<vietvm::runtime::StackValue>(16,
            vietvm::runtime::make_int_value(0)));
    const auto arguments = [&]() {
        return std::vector<vietvm::runtime::StackValue>{
            vietvm::runtime::make_int_value(CLOCK_MONOTONIC), bytes};
    };
    const auto blocked = [&] (
        const vietvm::bytecode::ForeignFunctionDescriptor &binding,
        const std::vector<vietvm::runtime::StackValue> &args,
        const std::unordered_set<std::string> &permissions) {
        try {
            (void)executeForeignCall(binding, args, permissions, 0);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };
    expect(blocked(descriptor, arguments(),
                   std::unordered_set<std::string>{}),
           "clock_gettime yêu cầu quyền system.time.monotonic");
    auto forged = descriptor;
    forged.symbol = "getpid";
    expect(blocked(forged, arguments(), caps),
           "không dùng quyền đồng hồ để gọi symbol khác");
    forged = descriptor;
    forged.result = ForeignAbiType::BufferOut;
    expect(blocked(forged, arguments(), caps),
           "buffer_out không được phép làm kiểu trả về");
    // Even a separately granted arbitrary native symbol must not gain a
    // writable pointer before explicit size/ownership descriptors exist.
    forged = descriptor;
    forged.capability = "test.ffi.buffer";
    expect(blocked(forged, arguments(), {"test.ffi.buffer"}),
           "không cho phép buffer_out với capability tùy ý được cấp quyền");
    forged = descriptor;
    forged.capability = "system.process.id";
    expect(blocked(forged, arguments(), {"system.process.id"}),
           "không dùng buffer_out dưới quyền kiểm tra PID");
    expect(blocked(descriptor,
                   {vietvm::runtime::make_int_value(CLOCK_MONOTONIC),
                    vietvm::runtime::make_string_value("bad")}, caps),
           "buffer_out từ chối đối số không phải danh sách byte");
    auto shortBuffer = vietvm::runtime::make_list_value(
        std::vector<vietvm::runtime::StackValue>(8,
            vietvm::runtime::make_int_value(0)));
    expect(blocked(descriptor,
                   {vietvm::runtime::make_int_value(CLOCK_MONOTONIC), shortBuffer},
                   caps), "clock_gettime từ chối buffer không đủ 16 byte");
    expect(blocked(descriptor,
                   {vietvm::runtime::make_int_value(-1), bytes}, caps),
           "clock_gettime từ chối clock id không hợp lệ trước call");
    auto hugeBuffer = vietvm::runtime::make_list_value(
        std::vector<vietvm::runtime::StackValue>(65537,
            vietvm::runtime::make_int_value(0)));
    expect(blocked(descriptor,
                   {vietvm::runtime::make_int_value(CLOCK_MONOTONIC), hugeBuffer},
                   caps), "buffer_out chặn allocation quá giới hạn");

    timespec before{};
    timespec after{};
    const int beforeStatus = ::clock_gettime(CLOCK_MONOTONIC, &before);
    const auto result = executeForeignCall(descriptor, arguments(), caps, 0);
    const int afterStatus = ::clock_gettime(CLOCK_MONOTONIC, &after);
    expect(beforeStatus == 0 && afterStatus == 0 &&
           std::holds_alternative<int>(result.value) &&
           std::get<int>(result.value) == 0,
           "clock_gettime qua libffi thành công");
    const auto &contents = std::get<vietvm::runtime::ListHandle>(bytes)->elements;
    const auto little64 = [&](std::size_t start) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            if (!std::holds_alternative<int>(contents[start + i])) return UINT64_MAX;
            const int byte = std::get<int>(contents[start + i]);
            if (byte < 0 || byte > 255) return UINT64_MAX;
            value |= static_cast<std::uint64_t>(byte) << (i * 8);
        }
        return value;
    };
    const auto seconds = little64(0);
    const auto nanos = little64(8);
    expect(seconds >= static_cast<std::uint64_t>(before.tv_sec) &&
           seconds <= static_cast<std::uint64_t>(after.tv_sec) &&
           nanos < 1000000000,
           "buffer_out sao chép timespec vào list chung đúng byte order");

    // CLOCK_REALTIME has its own narrowly bound grant. Neither grant may
    // select the other clock even when both have the identical C signature.
    auto realtime = descriptor;
    realtime.capability = "system.time.realtime";
    const std::unordered_set<std::string> realtimeCap = {"system.time.realtime"};
    const auto realtimeArgs = [&]() {
        return std::vector<vietvm::runtime::StackValue>{
            vietvm::runtime::make_int_value(CLOCK_REALTIME), bytes};
    };
    expect(blocked(realtime, realtimeArgs(), caps),
           "CLOCK_REALTIME yêu cầu quyền riêng system.time.realtime");
    expect(blocked(realtime, arguments(), realtimeCap),
           "quyền realtime không thể gọi CLOCK_MONOTONIC");
    expect(blocked(descriptor, realtimeArgs(), caps),
           "quyền monotonic không thể gọi CLOCK_REALTIME");
    auto forgedRealtime = realtime;
    forgedRealtime.symbol = "getpid";
    expect(blocked(forgedRealtime, realtimeArgs(), realtimeCap),
           "quyền realtime khóa symbol clock_gettime");
    forgedRealtime = realtime;
    forgedRealtime.library = "libc.dylib";
    expect(blocked(forgedRealtime, realtimeArgs(), realtimeCap),
           "quyền realtime khóa logical library");
    forgedRealtime = realtime;
    forgedRealtime.result = ForeignAbiType::I64;
    expect(blocked(forgedRealtime, realtimeArgs(), realtimeCap),
           "quyền realtime khóa ABI trả về i32");
    forgedRealtime = realtime;
    forgedRealtime.parameters = {ForeignAbiType::I32, ForeignAbiType::CString};
    expect(blocked(forgedRealtime, realtimeArgs(), realtimeCap),
           "quyền realtime khóa ABI buffer_out");
    expect(blocked(realtime,
                   {vietvm::runtime::make_int_value(CLOCK_REALTIME), shortBuffer},
                   realtimeCap), "realtime từ chối timespec không đủ 16 byte");
    timespec realtimeBefore{};
    timespec realtimeAfter{};
    const int realtimeBeforeStatus = ::clock_gettime(CLOCK_REALTIME, &realtimeBefore);
    const auto realtimeResult = executeForeignCall(realtime, realtimeArgs(), realtimeCap, 0);
    const int realtimeAfterStatus = ::clock_gettime(CLOCK_REALTIME, &realtimeAfter);
    expect(realtimeBeforeStatus == 0 && realtimeAfterStatus == 0 &&
           std::holds_alternative<int>(realtimeResult.value) &&
           std::get<int>(realtimeResult.value) == 0,
           "CLOCK_REALTIME qua libffi thành công");
    const auto realtimeSeconds = little64(0);
    expect(realtimeSeconds >= static_cast<std::uint64_t>(realtimeBefore.tv_sec) &&
           realtimeSeconds <= static_cast<std::uint64_t>(realtimeAfter.tv_sec) &&
           little64(8) < 1000000000,
           "timespec realtime sao chép đúng với clock POSIX");
#endif
}

void testForeignEntropyBuffer() {
#if defined(VPP_TEST_POSIX_LIBFFI)
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::StackValue;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_list_value;
    vietvm::bytecode::ForeignFunctionDescriptor descriptor;
    descriptor.library = "system.c";
    descriptor.symbol = "getentropy";
    descriptor.abi = "c";
    descriptor.parameters = {ForeignAbiType::BufferOut, ForeignAbiType::U64};
    descriptor.result = ForeignAbiType::I32;
    descriptor.capability = "system.entropy.read";
    descriptor.bufferExtents.push_back({0, 1, 0});
    const std::unordered_set<std::string> grant = {"system.entropy.read"};
    const auto bytes = [](std::size_t n) {
        return make_list_value(std::vector<StackValue>(n, make_int_value(0)));
    };
    const auto invoke = [&](const auto &binding, const auto &contents,
                            const StackValue &length, const auto &caps) {
        return executeForeignCall(binding, {contents, length}, caps, 0);
    };
    const auto rejects = [&](const auto &binding, const auto &contents,
                             const StackValue &length, const auto &caps) {
        try {
            (void)invoke(binding, contents, length, caps);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };

    auto small = bytes(1);
    auto badExtent = descriptor;
    badExtent.bufferExtents = {{0, -1, 2}};
    expect(rejects(badExtent, small, make_int_value(1), grant),
           "FFI bắt được độ dài cố định không khớp trước native call");
    badExtent = descriptor;
    badExtent.bufferExtents = {{0, 1, 0}, {0, 1, 0}};
    expect(rejects(badExtent, small, make_int_value(1), grant),
           "FFI từ chối descriptor trùng quan hệ độ dài đệm");
    badExtent = descriptor;
    badExtent.bufferExtents = {{1, -1, 1}};
    expect(rejects(badExtent, small, make_int_value(1), grant),
           "FFI từ chối áp metadata đệm lên tham số số nguyên");
    badExtent = descriptor;
    badExtent.bufferExtents = {{0, 2, 0}};
    expect(rejects(badExtent, small, make_int_value(1), grant),
           "FFI từ chối tham chiếu độ dài vượt số tham số");
    expect(rejects(descriptor, small, make_int_value(1),
                   std::unordered_set<std::string>{}),
           "getentropy cần quyền riêng system.entropy.read");
    auto forged = descriptor;
    forged.symbol = "getpid";
    expect(rejects(forged, small, make_int_value(1), grant),
           "getentropy grant không được gọi symbol khác");
    forged = descriptor;
    forged.result = ForeignAbiType::Void;
    expect(rejects(forged, small, make_int_value(1), grant),
           "getentropy grant khóa ABI kết quả");
    forged = descriptor;
    forged.parameters = {ForeignAbiType::CString, ForeignAbiType::U64};
    expect(rejects(forged, small, make_int_value(1), grant),
           "getentropy grant khóa tham số buffer_out");
    forged = descriptor;
    forged.library = "libc.dylib";
    expect(rejects(forged, small, make_int_value(1), grant),
           "getentropy grant khóa logical library");
    expect(rejects(descriptor, bytes(0), make_int_value(0), grant),
           "getentropy không chấp nhận buffer rỗng");
    expect(rejects(descriptor, bytes(257), make_int_value(257), grant),
           "getentropy không chấp nhận quá 256 byte/call");
    expect(rejects(descriptor, bytes(8), make_int_value(7), grant),
           "getentropy phát hiện chiều dài ngắn hơn buffer");
    expect(rejects(descriptor, bytes(8), make_int_value(9), grant),
           "getentropy phát hiện chiều dài dài hơn buffer");
    expect(rejects(descriptor, bytes(8), make_int_value(-1), grant),
           "getentropy từ chối size_t âm");
    expect(rejects(descriptor, bytes(8),
                   vietvm::runtime::make_string_value("18446744073709551615"), grant),
           "getentropy không cho size_t vượt buffer");

    // Read both ends of the supported size range through the actual OS ABI.
    const auto one = invoke(descriptor, small, make_int_value(1), grant);
    const auto full = bytes(256);
    const auto all = invoke(descriptor, full, make_int_value(256), grant);
    expect(std::holds_alternative<int>(one.value) &&
               std::get<int>(one.value) == 0 &&
               std::holds_alternative<int>(all.value) &&
               std::get<int>(all.value) == 0,
           "getentropy thực sự trả thành công với 1 và 256 byte");
    const auto &items = std::get<vietvm::runtime::ListHandle>(full)->elements;
    bool valid = items.size() == 256;
    bool anyNonzero = false;
    for (const auto &byte : items) {
        if (!std::holds_alternative<int>(byte) ||
            std::get<int>(byte) < 0 || std::get<int>(byte) > 255) valid = false;
        if (std::holds_alternative<int>(byte) && std::get<int>(byte) != 0) anyNonzero = true;
    }
    expect(valid && anyNonzero, "entropy được sao chép thành 256 byte V++ hợp lệ");
#endif
}

#if defined(VPP_TEST_POSIX_LIBFFI)
void testForeignLocalClockBuffer() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::StackValue;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_list_value;

    ForeignFunctionDescriptor binding;
    binding.library = "system.c";
    binding.symbol = "localtime_r";
    binding.abi = "c";
    binding.parameters = {ForeignAbiType::BufferIn, ForeignAbiType::BufferOut};
    binding.result = ForeignAbiType::PointerStatus;
    binding.capability = "system.time.local";
    const std::unordered_set<std::string> grant = {"system.time.local"};
    const auto input = [](std::time_t value) {
        const auto *ptr = reinterpret_cast<const unsigned char *>(&value);
        std::vector<StackValue> bytes;
        for (std::size_t i = 0; i < 8; ++i) {
            bytes.push_back(make_int_value(ptr[i]));
        }
        return make_list_value(std::move(bytes));
    };
    const auto output = [](std::size_t size, int byte = 0) {
        return make_list_value(std::vector<StackValue>(size, make_int_value(byte)));
    };
    const auto rejects = [&](const ForeignFunctionDescriptor &descriptor,
                             const std::vector<StackValue> &args,
                             const std::unordered_set<std::string> &caps) {
        try {
            (void)executeForeignCall(descriptor, args, caps, 0);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };
    const auto zero = input(static_cast<std::time_t>(0));
    expect(rejects(binding, {zero, output(64)}, {}),
           "localtime_r bắt buộc có grant system.time.local");
    auto forged = binding;
    forged.symbol = "gmtime_r";
    expect(rejects(forged, {zero, output(64)}, grant),
           "localtime_r không cho tráo gmtime_r");
    forged = binding;
    forged.library = "libc.dylib";
    expect(rejects(forged, {zero, output(64)}, grant),
           "localtime_r khóa logical library");
    forged = binding;
    forged.result = ForeignAbiType::CString;
    expect(rejects(forged, {zero, output(64)}, grant),
           "localtime_r cấm biến con trỏ tm thành chuỗi");
    forged = binding;
    forged.parameters[0] = ForeignAbiType::CString;
    expect(rejects(forged, {zero, output(64)}, grant),
           "localtime_r khóa chiều buffer đầu vào");
    expect(rejects(binding, {zero, output(56)}, grant),
           "localtime_r yêu cầu đệm tm đúng 64 byte");
    expect(rejects(binding, {output(7), output(64)}, grant),
           "localtime_r yêu cầu đúng 8 byte time_t");
    auto invalid = output(8, 255);
    std::get<vietvm::runtime::ListHandle>(invalid)->elements[0] =
        vietvm::runtime::make_string_value("bad");
    expect(rejects(binding, {invalid, output(64)}, grant),
           "localtime_r chặn byte sai kiểu trước native call");

    for (const std::time_t epoch : {static_cast<std::time_t>(-1),
                                    static_cast<std::time_t>(0),
                                    static_cast<std::time_t>(1710054000),
                                    static_cast<std::time_t>(1730613600)}) {
        std::tm reference{};
        expect(::localtime_r(&epoch, &reference) != nullptr,
               "fixture native localtime_r hỗ trợ epoch kiểm thử");
        const auto bytes = output(64, 91);
        const auto result = executeForeignCall(binding, {input(epoch), bytes}, grant, 0);
        expect(result.value == make_int_value(1),
               "FFI localtime_r trả trạng thái pointer 1");
        const auto &elements =
            std::get<vietvm::runtime::ListHandle>(bytes)->elements;
        std::vector<unsigned char> raw;
        for (const auto &item : elements) {
            if (!std::holds_alternative<int>(item) ||
                std::get<int>(item) < 0 || std::get<int>(item) > 255) break;
            raw.push_back(static_cast<unsigned char>(std::get<int>(item)));
        }
        expect(raw.size() == 64, "localtime_r chỉ sao chép byte hợp lệ");
        if (raw.size() != 64) continue;
        std::tm decoded{};
        std::memcpy(&decoded, raw.data(), sizeof(decoded));
        expect(decoded.tm_sec == reference.tm_sec &&
                   decoded.tm_min == reference.tm_min &&
                   decoded.tm_hour == reference.tm_hour &&
                   decoded.tm_mday == reference.tm_mday &&
                   decoded.tm_mon == reference.tm_mon &&
                   decoded.tm_year == reference.tm_year &&
                   decoded.tm_isdst == reference.tm_isdst &&
                   decoded.tm_gmtoff == reference.tm_gmtoff,
               "FFI localtime_r giữ đầy đủ civil time, timezone và DST");
    }
    const auto huge = input(std::numeric_limits<std::time_t>::max());
    const auto unchanged = output(64, 71);
    const auto overflow = executeForeignCall(binding, {huge, unchanged}, grant, 0);
    if (overflow.value == make_int_value(0)) {
        bool same = true;
        for (const auto &item :
             std::get<vietvm::runtime::ListHandle>(unchanged)->elements) {
            if (!(item == make_int_value(71))) same = false;
        }
        expect(same, "localtime_r thất bại không ghi đệm trả về V++");
    }
}

void testForeignFilesystemBindings() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::ForeignFileState;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_list_value;
    using vietvm::runtime::make_null_value;
    using vietvm::runtime::make_string_value;

    ForeignFunctionDescriptor mkdirBinding;
    mkdirBinding.library = "system.c";
    mkdirBinding.symbol = "mkdir";
    mkdirBinding.abi = "c";
    mkdirBinding.parameters = {ForeignAbiType::CString, ForeignAbiType::U32};
    mkdirBinding.result = ForeignAbiType::I32;
    mkdirBinding.capability = "system.fs.mkdir";
    ForeignFunctionDescriptor removeBinding = mkdirBinding;
    removeBinding.symbol = "remove";
    removeBinding.parameters = {ForeignAbiType::CString};
    removeBinding.capability = "system.fs.remove";
    ForeignFunctionDescriptor existsBinding = mkdirBinding;
    existsBinding.symbol = "access";
    existsBinding.parameters = {ForeignAbiType::CString, ForeignAbiType::I32};
    existsBinding.capability = "system.fs.exists";
    ForeignFunctionDescriptor statBinding = mkdirBinding;
    statBinding.symbol = "stat";
    statBinding.parameters = {ForeignAbiType::CString, ForeignAbiType::BufferOut};
    statBinding.capability = "system.fs.stat";
    ForeignFunctionDescriptor lstatBinding = statBinding;
    lstatBinding.symbol = "lstat";
    lstatBinding.capability = "system.fs.lstat";
    ForeignFunctionDescriptor layoutBinding;
    layoutBinding.library = "system.ffi";
    layoutBinding.symbol = "stat_mode_offset";
    layoutBinding.abi = "c";
    layoutBinding.result = ForeignAbiType::I32;
    layoutBinding.capability = "system.ffi.layout";
    ForeignFunctionDescriptor dirOpenBinding = mkdirBinding;
    dirOpenBinding.symbol = "opendir";
    dirOpenBinding.parameters = {ForeignAbiType::CString};
    dirOpenBinding.result = ForeignAbiType::DirectoryHandle;
    dirOpenBinding.capability = "system.fs.dir.open";
    ForeignFunctionDescriptor dirReadBinding = dirOpenBinding;
    dirReadBinding.symbol = "readdir";
    dirReadBinding.parameters = {ForeignAbiType::DirectoryHandle};
    dirReadBinding.result = ForeignAbiType::DirectoryEntry;
    dirReadBinding.capability = "system.fs.dir.read";
    ForeignFunctionDescriptor dirCloseBinding = dirReadBinding;
    dirCloseBinding.symbol = "closedir";
    dirCloseBinding.result = ForeignAbiType::I32;
    dirCloseBinding.capability = "system.fs.dir.close";
    const std::unordered_set<std::string> mkdirGrant = {"system.fs.mkdir"};
    const std::unordered_set<std::string> removeGrant = {"system.fs.remove"};
    const std::unordered_set<std::string> existsGrant = {"system.fs.exists"};
    const std::unordered_set<std::string> statGrant = {"system.fs.stat"};
    const std::unordered_set<std::string> lstatGrant = {"system.fs.lstat"};
    const std::unordered_set<std::string> layoutGrant = {"system.ffi.layout"};
    const std::unordered_set<std::string> dirOpenGrant = {"system.fs.dir.open"};
    const std::unordered_set<std::string> dirReadGrant = {"system.fs.dir.read"};
    const std::unordered_set<std::string> dirCloseGrant = {"system.fs.dir.close"};
    ForeignFileState dirStateA;
    ForeignFileState dirStateB;
    const auto dirCall = [&](const ForeignFunctionDescriptor &binding,
                             const std::vector<vietvm::runtime::StackValue> &args,
                             const std::unordered_set<std::string> &grant,
                             ForeignFileState *state = nullptr) {
        return executeForeignCall(binding, args, grant, 0,
                                  state == nullptr ? &dirStateA : state);
    };
    const auto path = make_string_value("/tmp/vpp-ffi-nonexistent-fixture-0000");
    const auto mode = make_int_value(0777);
    const auto buffer = [&](std::size_t length, int fill = 0) {
        return make_list_value(std::vector<vietvm::runtime::StackValue>(
            length, make_int_value(fill)));
    };
    const auto rejects = [&](const ForeignFunctionDescriptor &binding,
                             const std::vector<vietvm::runtime::StackValue> &args,
                             const std::unordered_set<std::string> &grants) {
        try {
            (void)executeForeignCall(binding, args, grants, 0);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };

    expect(rejects(mkdirBinding, {path, mode}, removeGrant),
           "mkdir không thể dùng quyền remove");
    expect(rejects(removeBinding, {path}, mkdirGrant),
           "remove không thể dùng quyền mkdir");
    expect(rejects(existsBinding, {path, make_int_value(0)}, mkdirGrant),
           "access cần quyền kiểm tra tồn tại riêng");
    auto forgedExists = existsBinding;
    forgedExists.symbol = "unlink";
    expect(rejects(forgedExists, {path, make_int_value(0)}, existsGrant),
           "quyền tồn tại không được dùng để xóa tệp");
    forgedExists = existsBinding;
    forgedExists.library = "libc.dylib";
    expect(rejects(forgedExists, {path, make_int_value(0)}, existsGrant),
           "quyền tồn tại khóa library");
    forgedExists = existsBinding;
    forgedExists.parameters = {ForeignAbiType::CString};
    expect(rejects(forgedExists, {path}, existsGrant),
           "quyền tồn tại khóa chữ ký");
    forgedExists = existsBinding;
    forgedExists.result = ForeignAbiType::U32;
    expect(rejects(forgedExists, {path, make_int_value(0)}, existsGrant),
           "quyền tồn tại khóa kiểu trả về");
    expect(rejects(existsBinding, {path, make_int_value(4)}, existsGrant),
           "quyền tồn tại chặn R_OK, chỉ cho phép F_OK");
    expect(rejects(existsBinding,
                   {make_string_value(std::string("x\0y", 3)), make_int_value(0)},
                   existsGrant), "quyền tồn tại chặn NUL trong đường dẫn");
    expect(rejects(statBinding, {path, buffer(256)}, existsGrant),
           "stat cần quyền system.fs.stat riêng");
    expect(rejects(lstatBinding, {path, buffer(256)}, statGrant),
           "lstat không dùng chung quyền stat");
    expect(rejects(statBinding, {path, buffer(256)}, lstatGrant),
           "stat không dùng chung quyền lstat");
    expect(rejects(layoutBinding, {}, statGrant),
           "truy vấn layout cần quyền system.ffi.layout riêng");
    auto forgedStat = statBinding;
    forgedStat.symbol = "lstat";
    expect(rejects(forgedStat, {path, buffer(256)}, statGrant),
           "system.fs.stat khóa symbol chống tráo lstat");
    forgedStat = lstatBinding;
    forgedStat.symbol = "stat";
    expect(rejects(forgedStat, {path, buffer(256)}, lstatGrant),
           "system.fs.lstat khóa symbol chống tráo stat");
    forgedStat = statBinding;
    forgedStat.library = "libc.dylib";
    expect(rejects(forgedStat, {path, buffer(256)}, statGrant),
           "stat khóa tên thư viện");
    forgedStat = statBinding;
    forgedStat.parameters[1] = ForeignAbiType::BufferIn;
    expect(rejects(forgedStat, {path, buffer(256)}, statGrant),
           "stat khóa hướng buffer");
    forgedStat = statBinding;
    forgedStat.result = ForeignAbiType::Void;
    expect(rejects(forgedStat, {path, buffer(256)}, statGrant),
           "stat khóa kiểu kết quả");
    forgedStat = statBinding;
    forgedStat.abi = "stdcall";
    expect(rejects(forgedStat, {path, buffer(256)}, statGrant),
           "stat khóa ABI");
    auto forgedLayout = layoutBinding;
    forgedLayout.symbol = "last_errno";
    expect(rejects(forgedLayout, {}, layoutGrant),
           "layout không cấp quyền đọc symbol khác");
    forgedLayout = layoutBinding;
    forgedLayout.parameters = {ForeignAbiType::I32};
    expect(rejects(forgedLayout, {make_int_value(0)}, layoutGrant),
           "layout khóa chữ ký không tham số");
    expect(rejects(statBinding, {path, buffer(255)}, statGrant),
           "stat chặn đệm ngắn hơn 256 byte");
    expect(rejects(lstatBinding, {path, buffer(257)}, lstatGrant),
           "lstat chặn đệm dài hơn 256 byte");
    expect(rejects(statBinding,
                   {make_string_value(std::string("x\0y", 3)), buffer(256)},
                   statGrant), "stat chặn NUL nhúng");
    const auto layout = executeForeignCall(layoutBinding, {}, layoutGrant, EACCES);
    expect(std::holds_alternative<int>(layout.value) &&
               std::get<int>(layout.value) ==
                   static_cast<int>(offsetof(struct stat, st_mode)) &&
               layout.posixError == EACCES,
           "layout trả offset st_mode native và giữ nguyên errno VM");
    const auto modeOffset = static_cast<std::size_t>(offsetof(struct stat, st_mode));
    const auto typeOf = [&](const vietvm::runtime::StackValue &bytes) {
        const auto &items = std::get<vietvm::runtime::ListHandle>(bytes)->elements;
        return (std::get<int>(items[modeOffset]) +
                256 * std::get<int>(items[modeOffset + 1])) & 0170000;
    };
    auto forged = mkdirBinding;
    forged.symbol = "system";
    expect(rejects(forged, {path, mode}, mkdirGrant),
           "quyền mkdir không thể tráo symbol");
    forged = mkdirBinding;
    forged.parameters = {ForeignAbiType::CString, ForeignAbiType::I32};
    expect(rejects(forged, {path, mode}, mkdirGrant),
           "quyền mkdir khóa kiểu mode u32");
    forged = mkdirBinding;
    forged.result = ForeignAbiType::Void;
    expect(rejects(forged, {path, mode}, mkdirGrant),
           "quyền mkdir khóa kiểu trả về i32");
    forged = mkdirBinding;
    forged.library = "libc.dylib";
    expect(rejects(forged, {path, mode}, mkdirGrant),
           "quyền mkdir khóa thư viện system.c");
    forged = removeBinding;
    forged.symbol = "unlink";
    expect(rejects(forged, {path}, removeGrant),
           "quyền remove không thể tráo unlink");
    forged = removeBinding;
    forged.parameters = {ForeignAbiType::CString, ForeignAbiType::U32};
    expect(rejects(forged, {path, mode}, removeGrant),
           "quyền remove khóa số đối số");
    expect(rejects(mkdirBinding, {path, make_int_value(0775)}, mkdirGrant),
           "quyền mkdir chặn mode vượt contract 0777");
    expect(rejects(removeBinding, {make_string_value(std::string("a\0b", 3))},
                   removeGrant),
           "FFI c_chuỗi chặn NUL nhúng cho đường dẫn");

    ForeignFunctionDescriptor errnoBinding;
    errnoBinding.library = "system.ffi";
    errnoBinding.symbol = "enoent";
    errnoBinding.abi = "c";
    errnoBinding.result = ForeignAbiType::I32;
    errnoBinding.capability = "system.ffi.error";
    const std::unordered_set<std::string> errorGrant = {"system.ffi.error"};
    auto errnoResult = executeForeignCall(errnoBinding, {}, errorGrant, EACCES);
    expect(errnoResult.value == make_int_value(ENOENT) &&
               errnoResult.posixError == EACCES,
           "enoent trả hằng số, không ghi đè snapshot lỗi");
    errnoBinding.symbol = "eexist";
    errnoResult = executeForeignCall(errnoBinding, {}, errorGrant, EACCES);
    expect(errnoResult.value == make_int_value(EEXIST) &&
               errnoResult.posixError == EACCES,
           "eexist trả hằng số, không ghi đè snapshot lỗi");

    char directory[] = "/tmp/vpp-ffi-filesystem-XXXXXX";
    char *root = ::mkdtemp(directory);
    expect(root != nullptr, "tạo được fixture thư mục riêng biệt");
    if (root == nullptr) return;
    const auto child = make_string_value(std::string(root) + "/child");
    const auto missingOutput = buffer(256, 91);
    const auto missingStat = executeForeignCall(statBinding, {child, missingOutput},
                                                statGrant, 0);
    expect(missingStat.value == make_int_value(-1) &&
               missingStat.posixError == ENOENT,
           "stat thiếu đường dẫn trả ENOENT");
    bool bufferIntact = true;
    for (const auto &byte : std::get<vietvm::runtime::ListHandle>(missingOutput)->elements) {
        if (!(byte == make_int_value(91))) bufferIntact = false;
    }
    expect(bufferIntact, "stat thất bại không ghi bất kỳ byte nào ra V++");
    expect(rejects(dirOpenBinding, {child}, dirReadGrant),
           "opendir cần quyền mở thư mục riêng");
    expect(rejects(dirReadBinding, {make_int_value(1)}, dirOpenGrant),
           "readdir cần quyền đọc thư mục riêng");
    expect(rejects(dirCloseBinding, {make_int_value(1)}, dirReadGrant),
           "closedir cần quyền đóng thư mục riêng");
    auto forgedDir = dirOpenBinding;
    forgedDir.symbol = "opendir64";
    expect(rejects(forgedDir, {child}, dirOpenGrant),
           "quyền mở thư mục khóa symbol");
    forgedDir = dirReadBinding;
    forgedDir.parameters = {ForeignAbiType::FileHandle};
    expect(rejects(forgedDir, {make_int_value(1)}, dirReadGrant),
           "không thể tráo FILE* vào readdir");
    forgedDir = dirCloseBinding;
    forgedDir.result = ForeignAbiType::DirectoryEntry;
    expect(rejects(forgedDir, {make_int_value(1)}, dirCloseGrant),
           "quyền đóng thư mục khóa kết quả");
    forgedDir = dirOpenBinding;
    forgedDir.library = "libc.dylib";
    expect(rejects(forgedDir, {child}, dirOpenGrant),
           "quyền mở thư mục khóa thư viện");
    const auto absentDir = dirCall(dirOpenBinding, {child}, dirOpenGrant);
    expect(absentDir.value == make_null_value() &&
               absentDir.posixError == ENOENT && dirStateA.directories.empty(),
           "opendir thiếu đường dẫn trả null/ENOENT, không rò handle");
    const auto notFound = executeForeignCall(existsBinding, {child, make_int_value(0)},
                                             existsGrant, 0);
    expect(notFound.value == make_int_value(-1) &&
               notFound.posixError == ENOENT,
           "access thiếu đường dẫn trả ENOENT");
    const auto created = executeForeignCall(mkdirBinding, {child, mode}, mkdirGrant, 0);
    expect(created.value == make_int_value(0), "mkdir thực sự tạo thư mục POSIX");
    const auto found = executeForeignCall(existsBinding, {child, make_int_value(0)},
                                          existsGrant, 0);
    expect(found.value == make_int_value(0), "access thấy thư mục mới tạo");
    const auto directoryOutput = buffer(256);
    const auto directoryStat = executeForeignCall(
        statBinding, {child, directoryOutput}, statGrant, 0);
    expect(directoryStat.value == make_int_value(0) &&
               typeOf(directoryOutput) == S_IFDIR,
           "stat ghi mode thư mục vào đệm V++ đúng ABI");
    const auto exists = executeForeignCall(mkdirBinding, {child, mode}, mkdirGrant, 0);
    expect(exists.value == make_int_value(-1) && exists.posixError == EEXIST,
           "mkdir giữ errno EEXIST khi thư mục đã có");
    const std::string linkPath = std::string(root) + "/link";
    expect(::symlink("child", linkPath.c_str()) == 0,
           "fixture tạo được symlink đến thư mục");
    const auto foundLink = executeForeignCall(
        existsBinding, {make_string_value(linkPath), make_int_value(0)}, existsGrant, 0);
    expect(foundLink.value == make_int_value(0),
           "access F_OK đi theo symlink đến thư mục tồn tại");
    const auto rootDir = dirCall(dirOpenBinding,
        {make_string_value(root)}, dirOpenGrant);
    expect(std::holds_alternative<int>(rootDir.value) &&
               dirStateA.directories.size() == 1,
           "opendir trả token sở hữu bởi VM");
    if (std::holds_alternative<int>(rootDir.value)) {
        const auto token = rootDir.value;
        bool badOwnerRejected = false;
        try {
            (void)dirCall(dirReadBinding, {token}, dirReadGrant, &dirStateB);
        } catch (const vietvm::runtime::RuntimeError &) {
            badOwnerRejected = true;
        }
        expect(badOwnerRejected, "readdir từ chối token thuộc VM khác");
        std::unordered_set<std::string> names;
        bool endedCleanly = false;
        for (int i = 0; i < 32; ++i) {
            const auto item = dirCall(dirReadBinding, {token}, dirReadGrant);
            if (item.value == make_null_value()) {
                endedCleanly = item.posixError == 0;
                break;
            }
            if (std::holds_alternative<std::string>(item.value)) {
                names.insert(std::get<std::string>(item.value));
            }
        }
        expect(endedCleanly && names.count("child") == 1 &&
                   names.count("link") == 1,
               "readdir trả d_name và kết thúc EOF với errno=0");
        const auto closedDir = dirCall(dirCloseBinding, {token}, dirCloseGrant);
        expect(closedDir.value == make_int_value(0) &&
                   dirStateA.directories.empty(),
               "closedir giải phóng resource và token VM");
        bool closedTokenRejected = false;
        try {
            (void)dirCall(dirReadBinding, {token}, dirReadGrant);
        } catch (const vietvm::runtime::RuntimeError &) {
            closedTokenRejected = true;
        }
        expect(closedTokenRejected, "readdir từ chối handle đã đóng");
    }
    const auto followOutput = buffer(256);
    const auto noFollowOutput = buffer(256);
    const auto followed = executeForeignCall(statBinding,
        {make_string_value(linkPath), followOutput}, statGrant, 0);
    const auto notFollowed = executeForeignCall(lstatBinding,
        {make_string_value(linkPath), noFollowOutput}, lstatGrant, 0);
    expect(followed.value == make_int_value(0) &&
               notFollowed.value == make_int_value(0) &&
               typeOf(followOutput) == S_IFDIR &&
               typeOf(noFollowOutput) == S_IFLNK,
           "stat theo symlink, lstat đọc chính symlink");
    const auto removedLink = executeForeignCall(
        removeBinding, {make_string_value(linkPath)}, removeGrant, 0);
    expect(removedLink.value == make_int_value(0) &&
               ::access((std::string(root) + "/child").c_str(), F_OK) == 0,
           "remove symlink không đi theo và xóa thư mục đích");
    const auto removed = executeForeignCall(removeBinding, {child}, removeGrant, 0);
    expect(removed.value == make_int_value(0), "remove xóa thư mục rỗng");
    const std::string brokenPath = std::string(root) + "/broken";
    expect(::symlink("child", brokenPath.c_str()) == 0,
           "fixture tạo được dangling symlink");
    const auto brokenLink = executeForeignCall(
        existsBinding, {make_string_value(brokenPath), make_int_value(0)},
        existsGrant, 0);
    expect(brokenLink.value == make_int_value(-1) &&
               brokenLink.posixError == ENOENT,
           "access theo dangling symlink trả thiếu đường dẫn");
    const auto brokenFollow = buffer(256, 23);
    const auto brokenNoFollow = buffer(256);
    const auto brokenStat = executeForeignCall(statBinding,
        {make_string_value(brokenPath), brokenFollow}, statGrant, 0);
    const auto brokenLstat = executeForeignCall(lstatBinding,
        {make_string_value(brokenPath), brokenNoFollow}, lstatGrant, 0);
    expect(brokenStat.value == make_int_value(-1) &&
               brokenStat.posixError == ENOENT &&
               brokenLstat.value == make_int_value(0) &&
               typeOf(brokenNoFollow) == S_IFLNK,
           "dangling symlink: stat ENOENT, lstat thấy symlink");
    bufferIntact = true;
    for (const auto &byte : std::get<vietvm::runtime::ListHandle>(brokenFollow)->elements) {
        if (!(byte == make_int_value(23))) bufferIntact = false;
    }
    expect(bufferIntact, "stat trên dangling symlink không sao chép đệm lỗi");
    const auto missing = executeForeignCall(removeBinding, {child}, removeGrant, 0);
    expect(missing.value == make_int_value(-1) && missing.posixError == ENOENT,
           "remove trả ENOENT khi thiếu đường dẫn");
    (void)::rmdir((std::string(root) + "/child").c_str());
    (void)::unlink(linkPath.c_str());
    (void)::unlink(brokenPath.c_str());
    (void)::rmdir(root);
}

void testForeignFileBindings() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::ForeignFileState;
    using vietvm::runtime::StackValue;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_list_value;
    using vietvm::runtime::make_null_value;
    using vietvm::runtime::make_string_value;

    auto binding = [](const std::string &symbol, const std::string &capability,
                      std::vector<ForeignAbiType> parameters, ForeignAbiType result) {
        ForeignFunctionDescriptor descriptor;
        descriptor.library = "system.c";
        descriptor.symbol = symbol;
        descriptor.abi = "c";
        descriptor.parameters = std::move(parameters);
        descriptor.result = result;
        descriptor.capability = capability;
        return descriptor;
    };
    const auto open = binding("fopen", "system.file.open",
                              {ForeignAbiType::CString, ForeignAbiType::CString},
                              ForeignAbiType::FileHandle);
    const auto read = binding("fread", "system.file.read",
                              {ForeignAbiType::BufferOut, ForeignAbiType::U64,
                               ForeignAbiType::U64, ForeignAbiType::FileHandle},
                              ForeignAbiType::U64);
    const auto write = binding("fwrite", "system.file.write",
                               {ForeignAbiType::BufferIn, ForeignAbiType::U64,
                                ForeignAbiType::U64, ForeignAbiType::FileHandle},
                               ForeignAbiType::U64);
    const auto close = binding("fclose", "system.file.close",
                               {ForeignAbiType::FileHandle}, ForeignAbiType::I32);
    const auto error = binding("ferror", "system.file.error",
                               {ForeignAbiType::FileHandle}, ForeignAbiType::I32);
    const std::unordered_set<std::string> allGrants = {
        "system.file.open", "system.file.read", "system.file.write",
        "system.file.close", "system.file.error"};
    const std::unordered_set<std::string> openGrant = {"system.file.open"};
    ForeignFileState stateA;
    ForeignFileState stateB;
    const auto invoke = [&](const ForeignFunctionDescriptor &descriptor,
                            const std::vector<StackValue> &args,
                            ForeignFileState *owner = nullptr) {
        return executeForeignCall(descriptor, args, allGrants, 0,
                                  owner == nullptr ? &stateA : owner);
    };
    const auto rejects = [&](const ForeignFunctionDescriptor &descriptor,
                             const std::vector<StackValue> &args,
                             const std::unordered_set<std::string> &grants,
                             ForeignFileState *owner = nullptr) {
        try {
            (void)executeForeignCall(descriptor, args, grants, 0,
                                      owner == nullptr ? &stateA : owner);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };

    char tempDir[] = "/tmp/vpp-ffi-file-XXXXXX";
    const char *root = ::mkdtemp(tempDir);
    expect(root != nullptr, "fixture FFI tệp tạo thư mục riêng");
    if (root == nullptr) return;
    const std::string filename = std::string(root) + "/payload";
    const auto path = make_string_value(filename);
    const auto wb = make_string_value("wb");
    const auto rb = make_string_value("rb");

    expect(rejects(open, {path, wb}, {}), "fopen cần quyền mở tệp");
    expect(rejects(write, {}, openGrant), "fwrite cần quyền ghi tệp riêng");
    bool noRegistryRejected = false;
    try {
        (void)executeForeignCall(open, {path, wb}, allGrants, 0, nullptr);
    } catch (const vietvm::runtime::RuntimeError &) {
        noRegistryRejected = true;
    }
    expect(noRegistryRejected, "fopen yêu cầu registry thuộc VM");
    auto forged = open;
    forged.library = "libc.dylib";
    expect(rejects(forged, {path, wb}, allGrants), "fopen khóa thư viện");
    forged = open;
    forged.symbol = "system";
    expect(rejects(forged, {path, wb}, allGrants), "fopen khóa symbol");
    forged = open;
    forged.parameters = {ForeignAbiType::CString};
    expect(rejects(forged, {path}, allGrants), "fopen khóa chữ ký tham số");
    forged = open;
    forged.result = ForeignAbiType::CString;
    expect(rejects(forged, {path, wb}, allGrants), "fopen khóa kiểu trả về");
    forged = open;
    forged.abi = "stdcall";
    expect(rejects(forged, {path, wb}, allGrants), "fopen khóa ABI");
    forged = read;
    forged.symbol = "fwrite";
    expect(rejects(forged, {}, allGrants), "fread không cho tráo symbol");
    forged = write;
    forged.parameters[0] = ForeignAbiType::BufferOut;
    expect(rejects(forged, {}, allGrants), "fwrite khóa hướng buffer");
    forged = close;
    forged.symbol = "pclose";
    expect(rejects(forged, {}, allGrants), "fclose khóa symbol");
    forged = error;
    forged.result = ForeignAbiType::Void;
    expect(rejects(forged, {}, allGrants), "ferror khóa kiểu trả về");
    expect(rejects(open, {path, make_string_value("r+")}, allGrants),
           "fopen chặn chế độ ngoài rb/wb/ab");
    expect(rejects(open, {make_string_value(std::string("a\0b", 3)), wb}, allGrants),
           "fopen chặn đường dẫn chứa NUL");
    expect(rejects(open, {path, make_string_value(std::string("w\0b", 3))}, allGrants),
           "fopen chặn chế độ chứa NUL");
    expect(rejects(open, {make_int_value(1), wb}, allGrants),
           "fopen chặn đường dẫn sai kiểu");

    const auto created = invoke(open, {path, wb});
    expect(std::holds_alternative<int>(created.value) &&
               stateA.files.size() == 1,
           "fopen tạo token opaque và lưu FILE* trong VM");
    if (!std::holds_alternative<int>(created.value)) {
        (void)::unlink(filename.c_str());
        (void)::rmdir(root);
        return;
    }
    const auto token = created.value;
    const auto size = make_int_value(1);
    const auto count = make_int_value(4);
    const auto bytes = make_list_value({make_int_value(0), make_int_value(255),
                                        make_int_value(65), make_int_value(10)});
    const auto zeros = make_list_value(std::vector<StackValue>(4, make_int_value(0)));
    expect(rejects(write, {bytes, size, count, token}, openGrant),
           "quyền mở tệp không cấp quyền ghi");
    expect(rejects(write, {bytes, make_int_value(2), count, token}, allGrants),
           "fwrite khóa size=1");
    expect(rejects(write, {bytes, size, make_int_value(5), token}, allGrants),
           "fwrite khóa count bằng chiều dài đệm");
    expect(rejects(write, {make_list_value({make_int_value(256)}), size,
                            make_int_value(1), token}, allGrants),
           "fwrite từ chối byte ngoài miền");
    expect(rejects(write, {make_list_value({make_string_value("x")}), size,
                            make_int_value(1), token}, allGrants),
           "fwrite từ chối byte sai kiểu");
    const auto huge = make_list_value(std::vector<StackValue>(65537, make_int_value(0)));
    expect(rejects(write, {huge, size, make_int_value(65537), token}, allGrants),
           "fwrite giới hạn bộ đệm 65536 byte");
    expect(rejects(write, {make_list_value({}), size, make_int_value(0), token}, allGrants),
           "fwrite từ chối đệm trống tại ABI");
    expect(rejects(write, {bytes, size, count, make_int_value(-1)}, allGrants),
           "fwrite từ chối handle giả mạo");
    expect(rejects(read, {zeros, size, count, token}, {}, &stateA),
           "fread yêu cầu quyền đọc tệp");
    expect(rejects(read, {zeros, size, make_int_value(1), token}, allGrants),
           "fread khóa chiều dài đệm");

    const auto written = invoke(write, {bytes, size, count, token});
    expect(std::holds_alternative<vietvm::runtime::AbiInteger>(written.value) &&
               std::get<vietvm::runtime::AbiInteger>(written.value).unsignedValue == 4,
           "fwrite bảo toàn size_t u64 chính xác");
    expect(invoke(close, {token}).value == make_int_value(0) && stateA.files.empty(),
           "fclose giải phóng token sau ghi");
    expect(rejects(close, {token}, allGrants), "fclose hai lần bị chặn");
    expect(rejects(error, {token}, allGrants), "ferror token đã đóng bị chặn");

    const auto reopened = invoke(open, {path, rb});
    expect(std::holds_alternative<int>(reopened.value), "fopen mở lại file để đọc");
    if (std::holds_alternative<int>(reopened.value)) {
        const auto readToken = reopened.value;
        expect(std::get<int>(readToken) != std::get<int>(token),
               "token đã đóng không bao giờ cấp lại");
        expect(rejects(read, {zeros, size, count, token}, allGrants),
               "fread chặn token cũ dù có stream mới");
        const auto actual = invoke(read, {zeros, size, count, readToken});
        expect(std::holds_alternative<vietvm::runtime::AbiInteger>(actual.value) &&
                   std::get<vietvm::runtime::AbiInteger>(actual.value).unsignedValue == 4,
               "fread đọc 4 byte với kết quả size_t u64");
        const auto &values = std::get<vietvm::runtime::ListHandle>(zeros)->elements;
        expect(values.size() == 4 && std::holds_alternative<int>(values[0]) &&
                   std::holds_alternative<int>(values[1]) &&
                   std::holds_alternative<int>(values[2]) &&
                   std::holds_alternative<int>(values[3]) &&
                   std::get<int>(values[0]) == 0 && std::get<int>(values[1]) == 255 &&
                   std::get<int>(values[2]) == 65 && std::get<int>(values[3]) == 10,
               "fread ghi byte nhị phân 0x00 và 0xff về list V++");
        expect(invoke(error, {readToken}).value == make_int_value(0),
               "ferror không báo lỗi khi đọc hợp lệ");

        const auto otherOpen = invoke(open, {path, rb}, &stateB);
        expect(std::holds_alternative<int>(otherOpen.value) &&
                   std::get<int>(otherOpen.value) != std::get<int>(readToken),
               "các VM tạo token duy nhất toàn tiến trình");
        if (std::holds_alternative<int>(otherOpen.value)) {
            expect(rejects(read, {zeros, size, count, otherOpen.value}, allGrants, &stateA),
                   "VM A từ chối token thuộc VM B");
            expect(rejects(close, {readToken}, allGrants, &stateB),
                   "VM B từ chối token thuộc VM A");
        }
        stateB.closeAll();
        stateA.closeAll();
        expect(rejects(error, {readToken}, allGrants),
               "cleanup VM làm token cũ hết hiệu lực");
    }
    const auto missing = invoke(open, {make_string_value(filename + ".missing"), rb});
    expect(missing.value == make_null_value() && missing.posixError == ENOENT,
           "fopen thất bại giữ errno ENOENT và không tạo token");
    expect(stateA.files.empty(), "fopen thất bại không giữ FILE* trong registry");
    (void)::unlink(filename.c_str());
    (void)::rmdir(root);
}
#endif

#if defined(VPP_TEST_POSIX_LIBFFI)
void testForeignDnsResolverBindings() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::ForeignFileState;
    using vietvm::runtime::StackValue;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_string_value;

    ForeignFunctionDescriptor open;
    open.library = "system.net";
    open.symbol = "resolve_open";
    open.abi = "c";
    open.parameters = {ForeignAbiType::CString, ForeignAbiType::CString,
                       ForeignAbiType::I32};
    open.result = ForeignAbiType::DnsHandle;
    open.capability = "system.net.resolve.open";
    auto next = open;
    next.symbol = "resolve_next";
    next.parameters = {ForeignAbiType::DnsHandle};
    next.result = ForeignAbiType::CString;
    next.capability = "system.net.resolve.next";
    auto close = next;
    close.symbol = "resolve_close";
    close.result = ForeignAbiType::I32;
    close.capability = "system.net.resolve.close";

    const std::unordered_set<std::string> grants = {
        "system.net.resolve.open", "system.net.resolve.next", "system.net.resolve.close"};
    const std::unordered_set<std::string> onlyOpen = {"system.net.resolve.open"};
    const std::unordered_set<std::string> onlyNext = {"system.net.resolve.next"};
    const std::unordered_set<std::string> onlyClose = {"system.net.resolve.close"};
    ForeignFileState vmA;
    ForeignFileState vmB;
    const auto invoke = [&](const ForeignFunctionDescriptor &binding,
                            const std::vector<StackValue> &args,
                            const std::unordered_set<std::string> &permissions,
                            ForeignFileState *state) {
        return executeForeignCall(binding, args, permissions, EACCES, state);
    };
    const auto rejects = [&](const ForeignFunctionDescriptor &binding,
                             const std::vector<StackValue> &args,
                             const std::unordered_set<std::string> &permissions,
                             ForeignFileState *state) {
        try {
            (void)invoke(binding, args, permissions, state);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };
    const auto host = make_string_value("127.0.0.1");
    const auto service = make_string_value("8080");
    const auto stream = make_int_value(0);
    const auto args = std::vector<StackValue>{host, service, stream};

    expect(rejects(open, args, onlyNext, &vmA),
           "resolver open yêu cầu capability riêng");
    expect(rejects(open, args, onlyOpen, nullptr),
           "resolver không chạy khi thiếu registry VM");
    auto forged = open;
    forged.symbol = "system";
    expect(rejects(forged, args, grants, &vmA),
           "resolver chặn tráo symbol");
    forged = open;
    forged.library = "system.c";
    expect(rejects(forged, args, grants, &vmA),
           "resolver chặn tráo provider");
    forged = open;
    forged.abi = "stdcall";
    expect(rejects(forged, args, grants, &vmA),
           "resolver chặn tráo ABI");
    forged = open;
    forged.parameters[2] = ForeignAbiType::U32;
    expect(rejects(forged, args, grants, &vmA),
           "resolver chặn tráo tham số");
    forged = open;
    forged.result = ForeignAbiType::I32;
    expect(rejects(forged, args, grants, &vmA),
           "resolver chặn tráo kiểu kết quả");
    expect(rejects(open, {make_string_value(""), service, stream}, grants, &vmA),
           "resolver chặn hostname rỗng");
    expect(rejects(open, {make_string_value(std::string("a\0b", 3)), service,
                          stream}, grants, &vmA),
           "resolver chặn hostname chứa NUL");
    expect(rejects(open, {make_string_value(std::string(1025, 'a')), service,
                          stream}, grants, &vmA),
           "resolver giới hạn hostname 1024 byte");
    expect(rejects(open, {host, make_string_value("0"), stream}, grants, &vmA) &&
           rejects(open, {host, make_string_value("65536"), stream}, grants, &vmA) &&
           rejects(open, {host, make_string_value("http"), stream}, grants, &vmA),
           "resolver chặn port ngoài miền và service name");
    expect(rejects(open, {host, make_string_value(std::string("80\0x", 4)),
                          stream}, grants, &vmA),
           "resolver chặn port chứa NUL");
    expect(rejects(open, {host, service, make_int_value(2)}, grants, &vmA),
           "resolver chặn flag protocol lạ");
    expect(vmA.resolvers.empty(),
           "mọi yêu cầu resolver sai đều không cấp handle");

    const auto opened = invoke(open, args, onlyOpen, &vmA);
    expect(std::holds_alternative<int>(opened.value) &&
               opened.posixError == EACCES && vmA.resolvers.size() == 1,
           "resolve_open cấp token VM và bảo toàn snapshot errno");
    if (!std::holds_alternative<int>(opened.value)) return;
    const auto token = opened.value;
    expect(rejects(next, {token}, onlyOpen, &vmA) &&
               rejects(close, {token}, onlyNext, &vmA),
           "resolve_next/close cần quyền riêng");
    expect(rejects(next, {token}, grants, &vmB) &&
               rejects(close, {token}, grants, &vmB),
           "resolver token không truyền qua VM khác");
    expect(rejects(next, {make_int_value(-1)}, grants, &vmA) &&
               rejects(close, {make_string_value("1")}, grants, &vmA),
           "resolver chặn token giả và sai kiểu");
    const auto first = invoke(next, {token}, onlyNext, &vmA);
    expect(std::holds_alternative<std::string>(first.value) &&
               std::get<std::string>(first.value) == "127.0.0.1" &&
               first.posixError == EACCES,
           "resolver trả địa chỉ IPv4 và không lộ addrinfo*");
    const auto ended = invoke(next, {token}, onlyNext, &vmA);
    expect(ended.value == vietvm::runtime::make_null_value() &&
               invoke(next, {token}, onlyNext, &vmA).value ==
                   vietvm::runtime::make_null_value(),
           "resolver hết danh sách trả rỗng ổn định");
    expect(invoke(close, {token}, onlyClose, &vmA).value == make_int_value(1) &&
               vmA.resolvers.empty(),
           "resolve_close giải phóng addrinfo");
    expect(rejects(close, {token}, grants, &vmA) &&
               rejects(next, {token}, grants, &vmA),
           "resolver token đóng không thể dùng lại");

    const auto openedB = invoke(open, {make_string_value("::1"),
                                    make_string_value("80"), make_int_value(1)},
                                grants, &vmB);
    expect(std::holds_alternative<int>(openedB.value) &&
               std::get<int>(openedB.value) != std::get<int>(token),
           "resolver UDP IPv6 trả token mới");
    if (std::holds_alternative<int>(openedB.value)) {
        const auto address = invoke(next, {openedB.value}, grants, &vmB);
        expect(std::holds_alternative<std::string>(address.value) &&
                   std::get<std::string>(address.value) == "::1",
               "resolver numeric IPv6 nguyên vẹn");
        vmB.closeAll();
        expect(vmB.resolvers.empty() && rejects(next, {openedB.value}, grants, &vmB),
               "VM reset giải phóng resolver và vô hiệu token");
    }
}

void testForeignSocketBindings() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::ForeignFileState;
    using vietvm::runtime::StackValue;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_list_value;
    using vietvm::runtime::make_string_value;

    ForeignFunctionDescriptor connect;
    connect.library = "system.net";
    connect.symbol = "socket_connect";
    connect.abi = "c";
    connect.parameters = {ForeignAbiType::CString, ForeignAbiType::I32,
                          ForeignAbiType::I32, ForeignAbiType::I32};
    connect.result = ForeignAbiType::SocketHandle;
    connect.capability = "system.net.socket.connect";
    auto listen = connect;
    listen.symbol = "socket_listen";
    listen.parameters = {ForeignAbiType::I32, ForeignAbiType::I32};
    listen.capability = "system.net.socket.listen";
    auto accept = connect;
    accept.symbol = "socket_accept";
    accept.parameters = {ForeignAbiType::SocketHandle};
    accept.capability = "system.net.socket.accept";
    auto timeout = accept;
    timeout.symbol = "socket_timeout";
    timeout.parameters.push_back(ForeignAbiType::I32);
    timeout.result = ForeignAbiType::I32;
    timeout.capability = "system.net.socket.timeout";
    auto kind = accept;
    kind.symbol = "socket_kind";
    kind.result = ForeignAbiType::I32;
    kind.capability = "system.net.socket.kind";
    auto send = accept;
    send.library = "system.c";
    send.symbol = "send";
    send.parameters = {ForeignAbiType::SocketHandle, ForeignAbiType::BufferIn,
                       ForeignAbiType::U64, ForeignAbiType::I32};
    send.result = ForeignAbiType::I64;
    send.capability = "system.net.socket.send";
    send.bufferExtents = {{1, 2, 0}};
    auto recv = send;
    recv.symbol = "recv";
    recv.parameters[1] = ForeignAbiType::BufferOut;
    recv.capability = "system.net.socket.recv";
    auto close = kind;
    close.library = "system.c";
    close.symbol = "close";
    close.capability = "system.net.socket.close";
    auto tlsUpgrade = accept;
    tlsUpgrade.symbol = "tls_upgrade";
    tlsUpgrade.parameters = {ForeignAbiType::SocketHandle, ForeignAbiType::CString};
    tlsUpgrade.capability = "system.net.tls.upgrade";
    auto tlsSend = send;
    tlsSend.library = "system.net";
    tlsSend.symbol = "tls_send";
    tlsSend.parameters.pop_back();
    tlsSend.capability = "system.net.tls.send";
    auto tlsRecv = tlsSend;
    tlsRecv.symbol = "tls_recv";
    tlsRecv.parameters[1] = ForeignAbiType::BufferOut;
    tlsRecv.capability = "system.net.tls.recv";

    const std::unordered_set<std::string> grants = {
        connect.capability, listen.capability, accept.capability,
        timeout.capability, kind.capability, send.capability,
        recv.capability, close.capability, tlsUpgrade.capability,
        tlsSend.capability, tlsRecv.capability};
    ForeignFileState vmA;
    ForeignFileState vmB;
    const auto invoke = [&](const ForeignFunctionDescriptor &binding,
                            const std::vector<StackValue> &args,
                            const std::unordered_set<std::string> &perms,
                            ForeignFileState *state) {
        return executeForeignCall(binding, args, perms, EACCES, state);
    };
    const auto rejects = [&](const ForeignFunctionDescriptor &binding,
                             const std::vector<StackValue> &args,
                             const std::unordered_set<std::string> &perms,
                             ForeignFileState *state) {
        try {
            (void)invoke(binding, args, perms, state);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };
    const auto connectArgs = std::vector<StackValue>{
        make_string_value("127.0.0.1"), make_int_value(80),
        make_int_value(0), make_int_value(1000)};
    expect(rejects(connect, connectArgs, {}, &vmA) &&
               rejects(connect, connectArgs, grants, nullptr),
           "socket connect yêu cầu quyền riêng và registry VM");
    auto tlsArgs = std::vector<StackValue>{
        make_int_value(536870912), make_string_value("localhost")};
    auto tlsBytes = make_list_value({make_int_value(65)});
    expect(rejects(tlsUpgrade, tlsArgs, {}, &vmA) &&
               rejects(tlsUpgrade, tlsArgs, grants, nullptr) &&
               rejects(tlsSend, {tlsArgs[0], tlsBytes, make_int_value(1)},
                       grants, &vmA),
           "TLS yêu cầu capability, registry và token VM");
    auto forgedTls = tlsUpgrade;
    forgedTls.symbol = "socket_connect";
    expect(rejects(forgedTls, tlsArgs, grants, &vmA),
           "TLS chặn tráo symbol");
    forgedTls = tlsUpgrade;
    forgedTls.library = "system.c";
    expect(rejects(forgedTls, tlsArgs, grants, &vmA),
           "TLS chặn tráo provider");
    forgedTls = tlsUpgrade;
    forgedTls.result = ForeignAbiType::I32;
    expect(rejects(forgedTls, tlsArgs, grants, &vmA),
           "TLS chặn tráo kiểu trả về");
    forgedTls = tlsRecv;
    forgedTls.parameters[1] = ForeignAbiType::BufferIn;
    expect(rejects(forgedTls, {tlsArgs[0], tlsBytes, make_int_value(1)},
                   grants, &vmA), "TLS chặn tráo chiều đệm");
    forgedTls = tlsSend;
    forgedTls.bufferExtents = {{1, 0, 0}};
    expect(rejects(forgedTls, {tlsArgs[0], tlsBytes, make_int_value(1)},
                   grants, &vmA), "TLS kiểm tra buffer extent");
    auto forged = connect;
    forged.symbol = "system";
    expect(rejects(forged, connectArgs, grants, &vmA),
           "socket chặn tráo symbol");
    forged = connect;
    forged.library = "system.c";
    expect(rejects(forged, connectArgs, grants, &vmA),
           "socket chặn tráo provider");
    forged = connect;
    forged.parameters[2] = ForeignAbiType::U32;
    expect(rejects(forged, connectArgs, grants, &vmA),
           "socket chặn tráo tham số");
    forged = connect;
    forged.result = ForeignAbiType::I32;
    expect(rejects(forged, connectArgs, grants, &vmA),
           "socket chặn tráo kiểu trả về");
    forged = connect;
    forged.abi = "stdcall";
    expect(rejects(forged, connectArgs, grants, &vmA),
           "socket chặn ABI ngoài c");
    expect(rejects(connect, {make_string_value(""), make_int_value(80),
                             make_int_value(0), make_int_value(1000)}, grants, &vmA) &&
               rejects(connect, {make_string_value(std::string("a\0b", 3)),
                                 make_int_value(80), make_int_value(0),
                                 make_int_value(1000)}, grants, &vmA) &&
               rejects(connect, {make_string_value("127.0.0.1"),
                                 make_int_value(0), make_int_value(0),
                                 make_int_value(1000)}, grants, &vmA) &&
               rejects(connect, {make_string_value("127.0.0.1"),
                                 make_int_value(80), make_int_value(2),
                                 make_int_value(1000)}, grants, &vmA) &&
               rejects(connect, {make_string_value("127.0.0.1"),
                                 make_int_value(80), make_int_value(0),
                                 make_int_value(0)}, grants, &vmA),
           "socket connect từ chối IP/port/protocol/timeout lỗi");
    expect(rejects(listen, {make_int_value(0), make_int_value(64)}, grants, &vmA) &&
               rejects(listen, {make_int_value(8080), make_int_value(0)}, grants, &vmA),
           "socket listener kiểm tra cổng và backlog trước bind");

    const auto unknown = make_int_value(536870912);
    expect(rejects(kind, {unknown}, grants, &vmA) &&
               rejects(accept, {unknown}, grants, &vmA) &&
               rejects(close, {unknown}, grants, &vmA),
           "socket token giả không được chấp nhận");

    int pair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        // Some sandbox runners forbid creating sockets even without bind.
        expect(errno == EPERM || errno == EACCES,
               "socketpair chỉ có thể bị bỏ qua khi sandbox từ chối");
        return;
    }
    const auto left = make_int_value(536870912);
    const auto right = make_int_value(536870913);
    vmA.sockets.emplace(536870912, ForeignFileState::Socket{pair[0], false, false});
    vmA.sockets.emplace(536870913, ForeignFileState::Socket{pair[1], false, false});
    expect(rejects(kind, {left}, grants, &vmB) &&
               rejects(send, {left, make_list_value({make_int_value(1)}),
                              make_int_value(1), make_int_value(0)}, grants, &vmB),
           "socket token không thể dùng từ VM khác");
    expect(invoke(kind, {left}, {kind.capability}, &vmA).value == make_int_value(0),
           "socket stream báo loại chính xác");
    expect(invoke(timeout, {left, make_int_value(1000)},
                  {timeout.capability}, &vmA).value == make_int_value(1),
           "socket timeout được cấu hình bằng adapter");
    expect(rejects(tlsUpgrade, {left, make_string_value("")}, grants, &vmA) &&
               rejects(tlsUpgrade, {left, make_string_value(std::string("a\0b", 3))},
                       grants, &vmA),
           "TLS chặn hostname rỗng và nhúng NUL");
    vmA.sockets.at(536870912).datagram = true;
    expect(rejects(tlsUpgrade, {left, make_string_value("localhost")}, grants, &vmA),
           "TLS không cho nâng cấp UDP");
    vmA.sockets.at(536870912).datagram = false;
    const auto input = make_list_value({make_int_value(0), make_int_value(255),
                                       make_int_value(65), make_int_value(0)});
    expect(rejects(send, {left, input, make_int_value(4), make_int_value(0)},
                   {recv.capability}, &vmA),
           "socket send yêu cầu quyền riêng");
    forged = send;
    forged.parameters[1] = ForeignAbiType::BufferOut;
    expect(rejects(forged, {left, input, make_int_value(4), make_int_value(0)},
                   grants, &vmA), "socket send không cho đổi hướng đệm");
    expect(rejects(send, {left, input, make_int_value(3), make_int_value(0)},
                   grants, &vmA) &&
               rejects(send, {left, input, make_int_value(4), make_int_value(1)},
                       grants, &vmA) &&
               rejects(send, {left, make_list_value({make_int_value(256)}),
                              make_int_value(1), make_int_value(0)}, grants, &vmA),
           "socket send ràng buộc chiều dài/flags/byte");
    const auto sent = invoke(send, {left, input, make_int_value(4), make_int_value(0)},
                             {send.capability}, &vmA);
    expect(std::holds_alternative<vietvm::runtime::AbiInteger>(sent.value) &&
               std::get<vietvm::runtime::AbiInteger>(sent.value).signedValue == 4,
           "socket send libffi gửi đủ dữ liệu nhị phân và trả i64 chính xác");
    const auto output = make_list_value(std::vector<StackValue>(4, make_int_value(77)));
    const auto received = invoke(recv, {right, output, make_int_value(4),
                                       make_int_value(0)}, {recv.capability}, &vmA);
    const auto &bytes = std::get<vietvm::runtime::ListHandle>(output)->elements;
    expect(std::holds_alternative<vietvm::runtime::AbiInteger>(received.value) &&
               std::get<vietvm::runtime::AbiInteger>(received.value).signedValue == 4 &&
               bytes[0] == make_int_value(0) && bytes[1] == make_int_value(255) &&
               bytes[2] == make_int_value(65) && bytes[3] == make_int_value(0),
           "socket recv libffi sao chép chính xác cả byte NUL");
    vmA.sockets.at(536870912).listener = true;
    expect(rejects(send, {left, input, make_int_value(4), make_int_value(0)},
                   grants, &vmA) &&
               rejects(recv, {left, output, make_int_value(4), make_int_value(0)},
                       grants, &vmA),
           "socket listener không được gửi hoặc nhận trực tiếp");
    expect(invoke(close, {left}, {close.capability}, &vmA).value == make_int_value(0) &&
               rejects(close, {left}, grants, &vmA),
           "socket close thu hồi token một lần");
    // A TLS-marked VM socket must never fall back to libc send()/recv().
    // This deliberately synthetic provider pointer is removed before cleanup.
    vmA.sockets.at(536870913).tlsSession = reinterpret_cast<void *>(1);
    expect(invoke(kind, {right}, grants, &vmA).value == make_int_value(2) &&
               rejects(send, {right, make_list_value({make_int_value(42)}),
                              make_int_value(1), make_int_value(0)}, grants, &vmA) &&
               rejects(recv, {right, make_list_value({make_int_value(0)}),
                              make_int_value(1), make_int_value(0)}, grants, &vmA),
           "TLS socket chặn I/O plaintext và báo kind TLS");
    vmA.sockets.at(536870913).tlsSession = nullptr;
    vmA.closeAll();
    expect(vmA.sockets.empty() && rejects(kind, {right}, grants, &vmA),
           "socket còn mở được VM reset giải phóng và token hết hiệu lực");

    // Failed TLS handshakes consume both the descriptor and VM token.
    int failedPair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, failedPair) == 0) {
        // Keep the peer attached so getpeername() accepts the stream;
        // malformed plaintext followed by EOF must fail the TLS handshake.
        const char invalidTls[] = "not a TLS record";
        const auto peerWrote = ::send(failedPair[1], invalidTls,
                                      sizeof(invalidTls) - 1, 0);
        (void)::shutdown(failedPair[1], SHUT_WR);
        const int doomedToken = 536870914;
        vmA.sockets.emplace(doomedToken,
            ForeignFileState::Socket{failedPair[0], false, false});
        bool handshakeFailed = false;
        try {
            (void)invoke(tlsUpgrade,
                         {make_int_value(doomedToken), make_string_value("localhost")},
                         grants, &vmA);
        } catch (const vietvm::runtime::LanguageException &) {
            handshakeFailed = true;
        }
        expect(peerWrote == static_cast<ssize_t>(sizeof(invalidTls) - 1) &&
                   handshakeFailed && vmA.sockets.count(doomedToken) == 0 &&
                   ::fcntl(failedPair[0], F_GETFD) == -1,
               "TLS handshake lỗi đóng fd và thu hồi token đúng một lần");
        (void)::close(failedPair[1]);
    }
}

void testForeignProcessBindings() {
    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    using vietvm::runtime::executeForeignCall;
    using vietvm::runtime::ForeignFileState;
    using vietvm::runtime::StackValue;
    using vietvm::runtime::make_int_value;
    using vietvm::runtime::make_list_value;
    using vietvm::runtime::make_string_value;

    ForeignFunctionDescriptor create;
    create.library = "system.process";
    create.symbol = "new";
    create.abi = "c";
    create.parameters = {ForeignAbiType::CString};
    create.result = ForeignAbiType::I32;
    create.capability = "system.process.new";
    auto operation = [&](const std::string &name,
                         std::vector<ForeignAbiType> parameters,
                         ForeignAbiType result = ForeignAbiType::I32) {
        auto descriptor = create;
        descriptor.symbol = name;
        descriptor.capability = "system.process." + name;
        descriptor.parameters = std::move(parameters);
        descriptor.result = result;
        return descriptor;
    };
    const auto arg = operation("arg", {ForeignAbiType::I32, ForeignAbiType::CString});
    const auto commandLine = operation("command_line",
        {ForeignAbiType::I32, ForeignAbiType::CString});
    const auto env = operation("env", {ForeignAbiType::I32, ForeignAbiType::CString,
                                       ForeignAbiType::CString});
    const auto start = operation("start", {ForeignAbiType::I32});
    const auto poll = operation("poll", {ForeignAbiType::I32, ForeignAbiType::I32});
    const auto read = operation("read", {ForeignAbiType::I32, ForeignAbiType::I32,
                                        ForeignAbiType::BufferOut, ForeignAbiType::U64});
    const auto wait = operation("wait", {ForeignAbiType::I32});
    const auto error = operation("error", {ForeignAbiType::I32}, ForeignAbiType::CString);
    const auto close = operation("close", {ForeignAbiType::I32});
    const std::unordered_set<std::string> grants = {
        create.capability, arg.capability, env.capability, start.capability,
        poll.capability, read.capability, wait.capability, error.capability,
        close.capability};
    ForeignFileState vmA;
    ForeignFileState vmB;
    const auto call = [&](const ForeignFunctionDescriptor &binding,
                          const std::vector<StackValue> &params,
                          const std::unordered_set<std::string> &permissions,
                          ForeignFileState *state) {
        return executeForeignCall(binding, params, permissions, 0, state).value;
    };
    const auto rejects = [&](const ForeignFunctionDescriptor &binding,
                             const std::vector<StackValue> &params,
                             const std::unordered_set<std::string> &permissions,
                             ForeignFileState *state) {
        try {
            (void)call(binding, params, permissions, state);
            return false;
        } catch (const vietvm::runtime::RuntimeError &) {
            return true;
        }
    };
    const auto program = std::vector<StackValue>{make_string_value("/bin/sh")};
    expect(rejects(create, program, {}, &vmA) &&
               rejects(create, program, grants, nullptr),
           "process cần capability và registry của VM");
    auto forged = create;
    forged.library = "system.c";
    expect(rejects(forged, program, grants, &vmA),
           "process không chấp nhận tráo library");
    forged = create;
    forged.symbol = "fork";
    expect(rejects(forged, program, grants, &vmA),
           "process không chấp nhận symbol chưa khai báo");
    forged = create;
    forged.parameters = {ForeignAbiType::I32};
    expect(rejects(forged, program, grants, &vmA),
           "process không chấp nhận tham số sai ABI");
    forged = create;
    forged.result = ForeignAbiType::CString;
    expect(rejects(forged, program, grants, &vmA),
           "process không chấp nhận kiểu trả về sai ABI");
    forged = create;
    forged.abi = "stdcall";
    expect(rejects(forged, program, grants, &vmA),
           "process chỉ hỗ trợ ABI c");
    expect(rejects(create, {make_string_value("")}, grants, &vmA) &&
               rejects(create, {make_string_value(std::string("/bin/sh\0x", 10))},
                       grants, &vmA),
           "process từ chối chương trình rỗng hoặc chứa NUL");

    const int id = std::get<int>(call(create, program, {create.capability}, &vmA));
    const auto token = make_int_value(id);
    const auto nonexistent = make_int_value(id + 100000);
    expect(rejects(start, {token}, grants, &vmB) &&
               rejects(close, {token}, grants, &vmB) &&
               rejects(error, {nonexistent}, grants, &vmA),
           "token process chỉ dùng được trong VM sở hữu");
    expect(rejects(arg, {token, make_string_value("-c")}, {create.capability}, &vmA) &&
               rejects(env, {token, make_string_value("A"), make_string_value("B")},
                       {arg.capability}, &vmA),
           "mỗi thao tác process cần quyền riêng");
    expect(rejects(env, {token, make_string_value("") , make_string_value("x")},
                   grants, &vmA) &&
               rejects(env, {token, make_string_value("A=B"), make_string_value("x")},
                       grants, &vmA) &&
               rejects(arg, {token, make_string_value(std::string("a\0b", 3))},
                       grants, &vmA),
           "argv/env không cho tên biến sai hoặc chuỗi chứa NUL");
    expect(rejects(poll, {token, make_int_value(0)}, grants, &vmA) &&
               rejects(wait, {token}, grants, &vmA),
           "không poll/wait trước khi spawn");
    expect(rejects(commandLine, {token, make_string_value("/bin/sh")},
                   grants, &vmA) &&
               rejects(commandLine, {token, make_string_value("/bin/sh")},
                   {commandLine.capability}, &vmA),
           "command_line Windows không mở khóa bằng quyền POSIX hay thiếu capability");

    expect(call(arg, {token, make_string_value("-c")}, grants, &vmA) == make_int_value(1) &&
               call(arg, {token, make_string_value(
                   "printf %s \"$VPP_FFI_PROC\"; printf error 1>&2; exit 9")},
                   grants, &vmA) == make_int_value(1) &&
               call(env, {token, make_string_value("VPP_FFI_PROC"),
                          make_string_value("child")}, grants, &vmA) == make_int_value(1),
           "process tạo argv/env theo nghĩa đen");
    expect(call(start, {token}, grants, &vmA) == make_int_value(1),
           "POSIX process spawn được từ FFI");
    expect(rejects(start, {token}, grants, &vmA) &&
               rejects(arg, {token, make_string_value("extra")}, grants, &vmA) &&
               rejects(env, {token, make_string_value("A"), make_string_value("B")},
                       grants, &vmA) &&
               rejects(wait, {token}, grants, &vmA),
           "sau spawn không được sửa argv/env, start lần hai hay wait trước EOF");
    const auto buffer = make_list_value(std::vector<StackValue>(32, make_int_value(0)));
    expect(rejects(read, {token, make_int_value(3), buffer, make_int_value(32)},
                   grants, &vmA) &&
               rejects(read, {token, make_int_value(1), buffer, make_int_value(31)},
                       grants, &vmA) &&
               rejects(poll, {token, make_int_value(5001)}, grants, &vmA),
           "poll/read kiểm tra channel, timeout và kích thước buffer");
    std::string out;
    std::string err;
    bool stdoutClosed = false;
    bool stderrClosed = false;
    for (int i = 0; i < 40 && !(stdoutClosed && stderrClosed); ++i) {
        const int events = std::get<int>(call(poll, {token, make_int_value(250)},
                                              grants, &vmA));
        for (int channel : {1, 2}) {
            if ((events & channel) == 0) continue;
            const int count = std::get<int>(call(read,
                {token, make_int_value(channel), buffer, make_int_value(32)},
                grants, &vmA));
            if (count == 0) {
                (channel == 1 ? stdoutClosed : stderrClosed) = true;
                continue;
            }
            auto &target = channel == 1 ? out : err;
            const auto &bytes = std::get<vietvm::runtime::ListHandle>(buffer)->elements;
            for (int index = 0; index < count; ++index)
                target.push_back(static_cast<char>(std::get<int>(bytes[index])));
        }
    }
    expect(stdoutClosed && stderrClosed && out == "child" && err == "error",
           "process poll/drain hai pipe và chuyển child-only env");
    if (stdoutClosed && stderrClosed) {
        expect(call(wait, {token}, grants, &vmA) == make_int_value(9),
               "process wait trả exit code không phải 0");
        expect(rejects(wait, {token}, grants, &vmA),
               "process chỉ wait một lần");
    }
    expect(call(close, {token}, grants, &vmA) == make_int_value(1) &&
               rejects(close, {token}, grants, &vmA),
           "process close một lần và thu hồi token");

    const int broken = std::get<int>(call(create,
        {make_string_value("/vpp/unknown-process-executable")}, grants, &vmA));
    const auto missing = make_int_value(broken);
    expect(call(start, {missing}, grants, &vmA) == make_int_value(0) &&
               !std::get<std::string>(call(error, {missing}, grants, &vmA)).empty() &&
               rejects(start, {missing}, grants, &vmA) &&
               rejects(arg, {missing, make_string_value("late")}, grants, &vmA),
           "spawn thất bại vẫn khóa trạng thái, báo lỗi và không chạy lại");

    // The fixture using public V++ APIs must not declare private FFI bindings.
    // Validate the low-level cancellation contract here, after a child-ready
    // marker proves the child ran. This avoids a race with the OS scheduler.
    const auto readyThenClose = [&](const std::string &command,
                                    const std::string &marker) {
        const int childId = std::get<int>(call(create, program, grants, &vmA));
        const auto childToken = make_int_value(childId);
        const bool configured =
            call(arg, {childToken, make_string_value("-c")}, grants, &vmA) ==
                make_int_value(1) &&
            call(arg, {childToken, make_string_value(command)}, grants, &vmA) ==
                make_int_value(1);
        const bool started = configured &&
            call(start, {childToken}, grants, &vmA) == make_int_value(1);
        expect(started, "process child-ready spawn thành công: " + marker);
        if (!started) {
            (void)call(close, {childToken}, grants, &vmA);
            return;
        }

        const auto bytes = make_list_value(
            std::vector<StackValue>(16, make_int_value(0)));
        std::string observed;
        for (int attempt = 0; attempt < 30 && observed.size() < marker.size(); ++attempt) {
            const int events = std::get<int>(call(
                poll, {childToken, make_int_value(1000)}, grants, &vmA));
            if (events == 4) break;
            if ((events & 1) == 0) continue;
            const int count = std::get<int>(call(read,
                {childToken, make_int_value(1), bytes, make_int_value(16)},
                grants, &vmA));
            if (count == 0) break;
            const auto &elements = std::get<vietvm::runtime::ListHandle>(bytes)->elements;
            for (int i = 0; i < count; ++i)
                observed.push_back(static_cast<char>(std::get<int>(elements[i])));
        }
        expect(observed == marker,
               "process poll/read nhận đủ child-ready marker: " + marker);

        const pid_t child = static_cast<pid_t>(vmA.processes.at(childId).pid);
        expect(child > 0 && ::getpgid(child) == child && child != ::getpgrp(),
               "process child-ready sở hữu process group riêng: " + marker);
        expect(call(close, {childToken}, grants, &vmA) == make_int_value(1) &&
                   rejects(close, {childToken}, grants, &vmA),
               "process close/cancel thu hồi token: " + marker);
        errno = 0;
        expect(::waitpid(child, nullptr, WNOHANG) == -1 && errno == ECHILD,
               "process close/cancel reap child leader: " + marker);
    };

    readyThenClose("printf partial; exec sleep 120", "partial");
    // Spawn a descendant before acknowledging readiness; it inherits the
    // group's pipes and must be cancelled along with the shell.
    readyThenClose("sleep 120 & printf group; wait", "group");
    vmA.closeAll();
    expect(vmA.processes.empty() && rejects(close, {missing}, grants, &vmA),
           "VM closeAll hủy process token còn tồn tại");
}
#endif

} // namespace

int main() {
    testVerificationCacheAndInvalidation();
    testModuleVerificationPrecedesSideEffects();
    testPeriodicGcKeepsCapacity();
    testLegacyIntrinsicUsesOpcodeDispatcher();
    testLegacyNetworkErrorCatchMatchesDirectOpcode();
    testRemovedEnvironmentOpcodeRejectInterpreterAndJit();
#if !defined(_WIN32)
    testRemovedPosixSystemOpcodesRejectInterpreterAndJit();
#endif
    testJitFunctionsBranchesAndIntrinsics();
    testJitVariablesFloatAndRepeat();
    testForeignVerificationPrecedesSideEffects();
    testForeignFfiCapabilityAndRuntime();
    testForeignProcessIdFfi();
    testForeignParentProcessIdFfi();
    testForeignSleepFfi();
    testForeignMonotonicClockBuffer();
    testForeignEntropyBuffer();
#if defined(VPP_TEST_POSIX_LIBFFI)
    testForeignLocalClockBuffer();
    testForeignFilesystemBindings();
    testForeignFileBindings();
    testForeignDnsResolverBindings();
    testForeignSocketBindings();
    testForeignProcessBindings();
    testForeignPosixErrorSnapshot();
    testForeignExact64BitIntegers();
#endif
    if (failures != 0) {
        std::cerr << failures << " runtime P0 hardening check(s) failed\n";
        return 1;
    }
    std::cout << "Runtime P0 hardening: PASS\n";
    return 0;
}
