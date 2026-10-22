#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/intrinsic.h"
#include "vpp/native/aot_runtime.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/time_clock.h"

namespace {
using namespace vietvm::runtime;

bool rejects(Opcode op, std::vector<StackValue> args,
             const std::unordered_set<std::string> &caps) {
    try { (void)executeTimeClockPrimitive(op, args, caps); }
    catch (const RuntimeError &) { return true; }
    return false;
}

bool decode(const StackValue &value, std::int64_t &seconds, std::int64_t &nanos) {
    auto list = std::get_if<ListHandle>(&value);
    if (!list || !*list || (*list)->elements.size() != 16) return false;
    std::uint64_t fields[2]{};
    for (unsigned i = 0; i < 16; ++i) {
        const auto *byte = std::get_if<int>(&(*list)->elements[i]);
        if (!byte || *byte < 0 || *byte > 255) return false;
        fields[i / 8] |= static_cast<std::uint64_t>(*byte) << ((i % 8) * 8);
    }
    static_assert(sizeof(fields[0]) == sizeof(seconds));
    std::memcpy(&seconds, &fields[0], sizeof(seconds));
    std::memcpy(&nanos, &fields[1], sizeof(nanos));
    return true;
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *why) {
        if (!ok) { std::cerr << why << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> mono{"system.time.monotonic"};
    const std::unordered_set<std::string> utc{"system.time.realtime"};
    check(isTimeClockPrimitive(OP_VM_MACHINE_TIME_MONOTONIC), "missing monotonic opcode");
    check(isTimeClockPrimitive(OP_VM_MACHINE_TIME_REALTIME), "missing realtime opcode");
    check(!isTimeClockPrimitive(OP_VM_MACHINE_TIME_SLEEP), "sleep opcode overlap");
    const auto *md = vietvm::bytecode::intrinsicByOpcode(OP_VM_MACHINE_TIME_MONOTONIC);
    const auto *rd = vietvm::bytecode::intrinsicByOpcode(OP_VM_MACHINE_TIME_REALTIME);
    check(md && md->arity == 0 && rd && rd->arity == 0, "machine clock arity mismatch");
    check(rejects(OP_VM_MACHINE_TIME_MONOTONIC, {}, {}), "monotonic missing capability");
    check(rejects(OP_VM_MACHINE_TIME_REALTIME, {}, {}), "realtime missing capability");
    check(rejects(OP_VM_MACHINE_TIME_MONOTONIC, {}, utc), "realtime grants monotonic");
    check(rejects(OP_VM_MACHINE_TIME_REALTIME, {}, mono), "monotonic grants realtime");
    check(rejects(OP_VM_MACHINE_TIME_REALTIME, {}, {"system.ffi.time"}),
          "FFI capability grants machine realtime");
    check(rejects(OP_VM_MACHINE_TIME_MONOTONIC, {1}, mono), "extra argument accepted");
    check(rejects(OP_VM_MACHINE_TIME_REALTIME, {make_null_value()}, utc),
          "realtime extra argument accepted");
    check(rejects(OP_VM_MACHINE_TIME_SLEEP, {}, mono), "foreign opcode accepted");

    std::int64_t m0 = 0, mn0 = 0, m1 = 0, mn1 = 0, rt = 0, rn = 0;
    check(decode(executeTimeClockPrimitive(OP_VM_MACHINE_TIME_MONOTONIC, {}, mono), m0, mn0),
          "monotonic wire format invalid");
    check(decode(executeTimeClockPrimitive(OP_VM_MACHINE_TIME_MONOTONIC, {}, mono), m1, mn1),
          "second monotonic wire format invalid");
    check(m0 >= 0 && m1 >= m0 && (m1 > m0 || mn1 >= mn0),
          "monotonic clock moved backwards");
    check(mn0 >= 0 && mn0 < 1000000000 && mn1 >= 0 && mn1 < 1000000000,
          "monotonic nanosecond range invalid");
    // Socket connect reads this exact clock boundary for its timeout deadline.
    std::int64_t socketMs0 = -1, socketMs1 = -1;
    const bool clockOk0 = machineMonotonicMilliseconds(socketMs0);
    const bool clockOk1 = machineMonotonicMilliseconds(socketMs1);
    check(clockOk0 && clockOk1, "socket machine monotonic clock unavailable");
    check(clockOk0 && clockOk1 && socketMs0 >= m1 * 1000 + mn1 / 1000000 &&
          socketMs1 >= socketMs0, "socket machine clock differs from V++ monotonic clock");
    check(decode(executeTimeClockPrimitive(OP_VM_MACHINE_TIME_REALTIME, {}, utc), rt, rn),
          "realtime wire format invalid");
    check(rn >= 0 && rn < 1000000000, "realtime nanosecond range invalid");
    const auto wallSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    check(rt >= wallSeconds - 10 && rt <= wallSeconds + 10,
          "realtime clock far from system time");

    vietvm::native::aot::Program program;
    program.capabilities = mono;
    vietvm::native::aot::Frame frame(program);
    frame.intrinsic(OP_VM_MACHINE_TIME_MONOTONIC);
    check(decode(frame.pop(), m0, mn0), "native AOT monotonic wire format invalid");
    bool denied = false;
    try { frame.intrinsic(OP_VM_MACHINE_TIME_REALTIME); }
    catch (const RuntimeError &) { denied = true; }
    check(denied, "native AOT bypassed capability isolation");
    program.capabilities = utc;
    frame.intrinsic(OP_VM_MACHINE_TIME_REALTIME);
    check(decode(frame.pop(), rt, rn), "native AOT realtime wire format invalid");

    if (!failures) std::cout << "machine clocks hardening passed\n";
    return failures == 0 ? 0 : 1;
}
