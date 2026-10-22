#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "vpp/bytecode/intrinsic.h"
#include "vpp/native/aot_runtime.h"
#include "vpp/runtime/environment_machine.h"
#include "vpp/runtime/error.h"

namespace {
using namespace vietvm::runtime;

bool rejects(int op, const std::vector<StackValue> &args,
             const std::unordered_set<std::string> &caps) {
    try { (void)executeEnvironmentMachinePrimitive(static_cast<Opcode>(op), args, caps); }
    catch (const RuntimeError &) { return true; }
    return false;
}

bool equals(const StackValue &value, const char *expected) {
    const auto *text = std::get_if<std::string>(&value);
    return text && *text == expected;
}

} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *why) {
        if (!ok) { std::cerr << why << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> env{"system.env.read"};
    const std::unordered_set<std::string> unrelated{"system.entropy.read"};
    const auto *descriptor = vietvm::bytecode::intrinsicByOpcode(OP_VM_MACHINE_ENV_GET);
    check(isEnvironmentMachinePrimitive(OP_VM_MACHINE_ENV_GET), "environment opcode absent");
    check(!isEnvironmentMachinePrimitive(OP_VM_MACHINE_ENTROPY_READ), "opcode collision");
    check(descriptor && descriptor->name == "máy_môi_trường_đọc" && descriptor->arity == 1,
          "wrong environment metadata");

    const auto name = make_string_value("VPP_MACHINE_ENV_TEST");
    check(rejects(OP_VM_MACHINE_ENV_GET, {name}, {}), "missing capability accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {name}, unrelated), "wrong grant accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {}, env), "missing argument accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {name, name}, env), "extra argument accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {5}, env), "integer name accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {make_null_value()}, env), "null name accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {make_string_value("")}, env), "empty name accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {make_string_value("A=B")}, env), "equals accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {make_string_value(std::string("A\0B", 3))}, env),
          "embedded NUL accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {name}, env), "foreign opcode accepted");

#if defined(_WIN32)
    const bool assigned = ::SetEnvironmentVariableW(L"VPP_MACHINE_ENV_TEST", L"máy-UTF8") != 0;
#else
    const bool assigned = ::setenv("VPP_MACHINE_ENV_TEST", "máy-UTF8", 1) == 0;
#endif
    check(assigned, "could not set test environment");
    if (assigned) {
        check(equals(executeEnvironmentMachinePrimitive(OP_VM_MACHINE_ENV_GET, {name}, env),
                     "máy-UTF8"), "environment value wrong");
    }
    check(std::holds_alternative<std::monostate>(executeEnvironmentMachinePrimitive(
        OP_VM_MACHINE_ENV_GET, {make_string_value("VPP_MACHINE_NOT_PRESENT_02EF")}, env)),
        "missing environment must yield null");
#if !defined(_WIN32)
    check(::setenv("VPP_MACHINE_EMPTY_TEST", "", 1) == 0, "could not set empty env");
    check(equals(executeEnvironmentMachinePrimitive(
        OP_VM_MACHINE_ENV_GET, {make_string_value("VPP_MACHINE_EMPTY_TEST")}, env), ""),
        "empty variable is not an empty string");
#endif

    vietvm::native::aot::Program program;
    program.capabilities = env;
    vietvm::native::aot::Frame frame(program);
    frame.push(name);
    frame.intrinsic(OP_VM_MACHINE_ENV_GET);
    check(equals(frame.pop(), "máy-UTF8"), "AOT environment read wrong");
    program.capabilities = unrelated;
    bool denied = false;
    try { frame.push(name); frame.intrinsic(OP_VM_MACHINE_ENV_GET); }
    catch (const RuntimeError &) { denied = true; }
    check(denied, "AOT bypassed environment capability");

    if (failures == 0) std::cout << "machine environment hardening passed\n";
    return failures == 0 ? 0 : 1;
}
