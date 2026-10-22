#include <chrono>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/intrinsic.h"
#include "vpp/native/aot_runtime.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/time_sleep.h"

namespace {
using namespace vietvm::runtime;

bool rejects(Opcode opcode, const std::vector<StackValue> &args,
             const std::unordered_set<std::string> &capabilities) {
    try { (void)executeTimeSleepPrimitive(opcode, args, capabilities); }
    catch (const RuntimeError &) { return true; }
    return false;
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::cerr << message << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> granted{"system.time.sleep"};
    check(isTimeSleepPrimitive(OP_VM_MACHINE_TIME_SLEEP), "sleep opcode missing");
    check(!isTimeSleepPrimitive(OP_VM_MACHINE_PROCESS_EGID), "identity opcode overlap");
    check(!isTimeSleepPrimitive(OP_VM_NGU_MILI_GIAY), "legacy sleep opcode overlap");
    check(vietvm::bytecode::intrinsicByOpcode(OP_VM_MACHINE_TIME_SLEEP)->arity == 1,
          "sleep opcode arity mismatch");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {1}, {}), "sleep missing capability accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {1}, {"system.ffi.error"}),
          "FFI capability incorrectly grants machine sleep");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {}, granted), "empty argument accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {1, 1}, granted), "extra argument accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {-1}, granted), "negative duration accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {1000}, granted), "duration over 999ms accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {1.5}, granted), "floating duration accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {make_string_value("1")}, granted),
          "string duration accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP,
                  {make_abi_integer_value(AbiInteger::unsignedNumber(4294967295ULL))},
                  granted), "large boxed duration accepted");
    check(rejects(OP_VM_MACHINE_PROCESS_PID, {1}, granted), "foreign opcode accepted");
    check(std::get<int>(executeTimeSleepPrimitive(OP_VM_MACHINE_TIME_SLEEP, {0}, granted)) == 0,
          "zero-duration sleep failed");

    const auto start = std::chrono::steady_clock::now();
    const auto result = executeTimeSleepPrimitive(OP_VM_MACHINE_TIME_SLEEP, {3}, granted);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    check(std::holds_alternative<int>(result) && std::get<int>(result) == 0,
          "machine sleep syscall failed");
    check(elapsed >= 1 && elapsed < 2000, "machine sleep duration unreasonable");

    vietvm::native::aot::Program program;
    program.capabilities = granted;
    vietvm::native::aot::Frame frame(program);
    frame.push(make_int_value(1));
    frame.intrinsic(OP_VM_MACHINE_TIME_SLEEP);
    check(std::get<int>(frame.pop()) == 0, "native AOT sleep returned an error");
    program.capabilities.clear();
    frame.push(make_int_value(0));
    bool nativeDenied = false;
    try { frame.intrinsic(OP_VM_MACHINE_TIME_SLEEP); }
    catch (const RuntimeError &) { nativeDenied = true; }
    check(nativeDenied, "native AOT sleep bypasses capability");

    if (!failures) std::cout << "machine time sleep hardening passed\n";
    return failures == 0 ? 0 : 1;
}
