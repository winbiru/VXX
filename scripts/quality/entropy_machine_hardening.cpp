#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/intrinsic.h"
#include "vpp/native/aot_runtime.h"
#include "vpp/runtime/entropy_machine.h"
#include "vpp/runtime/error.h"

namespace {
using namespace vietvm::runtime;

bool rejects(int operation, const std::vector<StackValue> &args,
             const std::unordered_set<std::string> &caps) {
    try {
        (void)executeEntropyMachinePrimitive(static_cast<Opcode>(operation), args, caps);
    } catch (const RuntimeError &) {
        return true;
    }
    return false;
}

bool validBytes(const StackValue &value, std::size_t expected) {
    const auto *handle = std::get_if<ListHandle>(&value);
    if (!handle || !*handle || (*handle)->elements.size() != expected) return false;
    for (const auto &item : (*handle)->elements) {
        const auto *byte = std::get_if<int>(&item);
        if (!byte || *byte < 0 || *byte > 255) return false;
    }
    return true;
}

} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *why) {
        if (!ok) { std::cerr << why << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> entropy{"system.entropy.read"};
    const std::unordered_set<std::string> other{"system.time.realtime", "system.ffi.error"};
    const auto *descriptor = vietvm::bytecode::intrinsicByOpcode(OP_VM_MACHINE_ENTROPY_READ);
    check(isEntropyMachinePrimitive(OP_VM_MACHINE_ENTROPY_READ), "entropy opcode absent");
    check(!isEntropyMachinePrimitive(OP_VM_MACHINE_TIME_REALTIME), "opcode collision");
    check(descriptor && descriptor->arity == 1 &&
          descriptor->name == "máy_entropy_bytes", "wrong intrinsic metadata");

    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {1}, {}), "missing capability accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {1}, other), "unrelated capability accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {}, entropy), "zero args accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {1, 2}, entropy), "extra args accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {0}, entropy), "zero bytes accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {-1}, entropy), "negative accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {257}, entropy), "over-limit accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {2.5}, entropy), "fraction accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {make_string_value("3")}, entropy),
          "string accepted");
    check(rejects(OP_VM_MACHINE_ENTROPY_READ, {make_null_value()}, entropy),
          "null accepted");
    check(rejects(OP_VM_MACHINE_TIME_REALTIME, {1}, entropy), "foreign opcode accepted");

    for (int count : {1, 64, 256}) {
        try {
            check(validBytes(executeEntropyMachinePrimitive(
                                 OP_VM_MACHINE_ENTROPY_READ, {count}, entropy),
                             static_cast<std::size_t>(count)),
                  "wrong entropy result size or type");
        } catch (const RuntimeError &err) {
            std::cerr << "OS entropy read failed: " << err.what() << '\n';
            ++failures;
        }
    }

    vietvm::native::aot::Program program;
    program.capabilities = entropy;
    vietvm::native::aot::Frame frame(program);
    try {
        frame.push(256);
        frame.intrinsic(OP_VM_MACHINE_ENTROPY_READ);
        check(validBytes(frame.pop(), 256), "AOT entropy result invalid");
    } catch (const RuntimeError &err) {
        std::cerr << "AOT entropy read failed: " << err.what() << '\n';
        ++failures;
    }
    bool denied = false;
    program.capabilities = other;
    try {
        frame.push(1);
        frame.intrinsic(OP_VM_MACHINE_ENTROPY_READ);
    } catch (const RuntimeError &) { denied = true; }
    check(denied, "AOT bypassed entropy capability");

    if (failures == 0) std::cout << "machine entropy hardening passed\n";
    return failures == 0 ? 0 : 1;
}
