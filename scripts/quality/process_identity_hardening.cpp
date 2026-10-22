#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/runtime/process_identity.h"
#include "vpp/runtime/error.h"
#include "vpp/native/aot_runtime.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
using namespace vietvm::runtime;

constexpr std::array<Opcode, 6> operations{{
    OP_VM_MACHINE_PROCESS_PID, OP_VM_MACHINE_PROCESS_PPID,
    OP_VM_MACHINE_PROCESS_UID, OP_VM_MACHINE_PROCESS_EUID,
    OP_VM_MACHINE_PROCESS_GID, OP_VM_MACHINE_PROCESS_EGID}};
constexpr std::array<const char *, 6> grants{{
    "system.process.id", "system.process.parent_id",
    "system.user.id", "system.user.effective_id",
    "system.group.id", "system.group.effective_id"}};

std::uint64_t number(const StackValue &value) {
    if (const auto *scalar32 = std::get_if<int>(&value))
        return static_cast<std::uint64_t>(*scalar32);
    const auto *wide = std::get_if<AbiInteger>(&value);
    if (!wide || wide->isSigned) throw std::runtime_error("expected unsigned scalar");
    return wide->unsignedValue;
}

bool rejects(Opcode opcode, const std::vector<StackValue> &args,
             const std::unordered_set<std::string> &permissions) {
    try { (void)executeProcessIdentityPrimitive(opcode, args, permissions); }
    catch (const RuntimeError &) { return true; }
    return false;
}

} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string &message) {
        if (!ok) { std::cerr << message << '\n'; ++failures; }
    };
    check(!isProcessIdentityPrimitive(OP_VM_MACHINE_FILE_REMOVE), "file opcode overlap");
    check(!isProcessIdentityPrimitive(OP_FFI_CALL), "FFI opcode overlap");
    check(!isProcessIdentityPrimitive(OP_VM_MACHINE_PROCESS_EGID + 1), "range overflow");
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const std::unordered_set<std::string> grant{grants[i]};
        check(isProcessIdentityPrimitive(operations[i]), "identity opcode unregistered");
        check(rejects(operations[i], {}, {}), "missing capability accepted: " + std::string(grants[i]));
        check(rejects(operations[i], {0}, grant), "extra argument accepted: " + std::string(grants[i]));
        check(rejects(operations[i], {make_string_value("ignored")}, grant), "wrong argument accepted");
        for (std::size_t j = 0; j < operations.size(); ++j) {
            if (i != j)
                check(rejects(operations[j], {}, grant), "capabilities are not isolated");
        }
#if defined(_WIN32)
        if (i >= 2) {
            check(rejects(operations[i], {}, grant), "Windows UID/GID unexpectedly available");
            continue;
        }
#endif
        const auto result = executeProcessIdentityPrimitive(operations[i], {}, grant);
        check(std::holds_alternative<int>(result) || std::holds_alternative<AbiInteger>(result),
              "identity result is not an integer");
        check(number(result) <= UINT32_MAX, "identity result exceeds u32");
        if (i < 2) check(number(result) > 0, "PID/PPID must be positive");
        if (i == 0) {
#if defined(_WIN32)
            check(number(result) == GetCurrentProcessId(), "PID differs from Win32 API");
#else
            check(number(result) == static_cast<std::uint64_t>(::getpid()), "PID differs from host");
#endif
        }
        // AOT dispatch takes the same zero-arity opcode and independent grant.
        vietvm::native::aot::Program program;
        program.capabilities = grant;
        vietvm::native::aot::Frame frame(program);
        frame.intrinsic(operations[i]);
        check(number(frame.pop()) == number(result), "native AOT identity differs from VM");
        program.capabilities.clear();
        check(rejects(operations[i], {}, program.capabilities), "runtime grant unexpectedly retained");
        bool nativeDenied = false;
        try { frame.intrinsic(operations[i]); }
        catch (const RuntimeError &) { nativeDenied = true; }
        check(nativeDenied, "native AOT bypasses capability check");
    }
    check(rejects(static_cast<Opcode>(999), {}, {}), "unknown opcode accepted");
    if (!failures) std::cout << "process identity hardening passed\n";
    return failures == 0 ? 0 : 1;
}
