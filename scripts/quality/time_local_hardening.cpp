#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/intrinsic.h"
#include "vpp/native/aot_runtime.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/time_local.h"

namespace {
using namespace vietvm::runtime;

StackValue epochBytes(std::int64_t epoch) {
    std::vector<StackValue> bytes;
    const auto bits = static_cast<std::uint64_t>(epoch);
    for (unsigned i = 0; i < 8; ++i)
        bytes.push_back(make_int_value(static_cast<int>((bits >> (i * 8)) & 255)));
    return make_list_value(std::move(bytes));
}

bool rejects(int opcode, std::vector<StackValue> args,
             const std::unordered_set<std::string> &caps) {
    try { (void)executeTimeLocalPrimitive(static_cast<Opcode>(opcode), args, caps); }
    catch (const RuntimeError &) { return true; }
    return false;
}

const std::vector<StackValue> *wire(const StackValue &value) {
    const auto *list = std::get_if<ListHandle>(&value);
    return list && *list ? &(*list)->elements : nullptr;
}

std::int32_t getI32(const std::vector<StackValue> &bytes, unsigned at) {
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < 4; ++i)
        bits |= static_cast<std::uint32_t>(std::get<int>(bytes[at + i])) << (8 * i);
    if (bits & 0x80000000u)
        return static_cast<std::int32_t>(static_cast<std::int64_t>(bits) - 4294967296LL);
    return static_cast<std::int32_t>(bits);
}

std::int64_t getI64(const std::vector<StackValue> &bytes, unsigned at) {
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t>(std::get<int>(bytes[at + i])) << (8 * i);
    return (bits & (std::uint64_t{1} << 63)) != 0
        ? -1 - static_cast<std::int64_t>(~bits)
        : static_cast<std::int64_t>(bits);
}

bool calendarMatches(const StackValue &result, std::int64_t seconds) {
    const auto *raw = wire(result);
    if (!raw || raw->size() != 64) return false;
    for (const auto &item : *raw) {
        const auto *byte = std::get_if<int>(&item);
        if (!byte || *byte < 0 || *byte > 255) return false;
    }
    const std::time_t epoch = static_cast<std::time_t>(seconds);
    std::tm expected{};
#if defined(_WIN32)
    if (::localtime_s(&expected, &epoch) != 0) return false;
#else
    if (::localtime_r(&epoch, &expected) == nullptr) return false;
#endif
    const bool fields = getI32(*raw, 0) == expected.tm_sec &&
        getI32(*raw, 4) == expected.tm_min && getI32(*raw, 8) == expected.tm_hour &&
        getI32(*raw, 12) == expected.tm_mday && getI32(*raw, 16) == expected.tm_mon &&
        getI32(*raw, 20) == expected.tm_year && getI32(*raw, 24) == expected.tm_wday &&
        getI32(*raw, 28) == expected.tm_yday && getI32(*raw, 32) == expected.tm_isdst;
#if defined(_WIN32)
    return fields && getI64(*raw, 40) == 0;
#else
    return fields && getI64(*raw, 40) == expected.tm_gmtoff;
#endif
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *reason) {
        if (!ok) { std::cerr << reason << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> grant{"system.time.local"};
    const std::unordered_set<std::string> unrelated{"system.time.realtime"};
    const auto *metadata = vietvm::bytecode::intrinsicByOpcode(OP_VM_MACHINE_TIME_LOCAL);
    check(isTimeLocalPrimitive(OP_VM_MACHINE_TIME_LOCAL), "localtime opcode missing");
    check(!isTimeLocalPrimitive(OP_VM_MACHINE_ENV_GET), "localtime opcode collision");
    check(metadata && metadata->name == "máy_đồng_hồ_địa_phương" && metadata->arity == 1,
          "wrong localtime descriptor");
    const StackValue epoch = epochBytes(1710053999); // DST spring transition (US)
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {epoch}, {}), "capability bypass");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {epoch}, unrelated), "capability isolation");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {}, grant), "missing argument accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {epoch, epoch}, grant), "extra argument accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {42}, grant), "non-list accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {make_null_value()}, grant), "null accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL, {make_list_value({})}, grant), "short bytes accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL,
                  {make_list_value({0, 0, 0, 0, 0, 0, 0, 0, 0})}, grant),
          "long bytes accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL,
                  {make_list_value({0, 0, 0, 0, 0, 0, 0, -1})}, grant),
          "negative byte accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL,
                  {make_list_value({0, 0, 0, 0, 0, 0, 0, 256})}, grant),
          "out-of-range byte accepted");
    check(rejects(OP_VM_MACHINE_TIME_LOCAL,
                  {make_list_value({0, 0, 0, 0, 0, 0, 0, 3.5})}, grant),
          "fraction byte accepted");
    check(rejects(OP_VM_MACHINE_ENV_GET, {epoch}, grant), "foreign opcode accepted");

    check(calendarMatches(executeTimeLocalPrimitive(OP_VM_MACHINE_TIME_LOCAL, {epoch}, grant),
                          1710053999), "DST fixture differs from host localtime");
    check(calendarMatches(executeTimeLocalPrimitive(OP_VM_MACHINE_TIME_LOCAL,
                          {epochBytes(0)}, grant), 0), "epoch zero differs from host");
#if !defined(_WIN32)
    check(calendarMatches(executeTimeLocalPrimitive(OP_VM_MACHINE_TIME_LOCAL,
                          {epochBytes(-1)}, grant), -1), "negative epoch differs from host");
#endif
    vietvm::native::aot::Program program;
    program.capabilities = grant;
    vietvm::native::aot::Frame frame(program);
    frame.push(epoch);
    frame.intrinsic(OP_VM_MACHINE_TIME_LOCAL);
    check(calendarMatches(frame.pop(), 1710053999), "AOT returned different localtime");
    program.capabilities = unrelated;
    bool denied = false;
    try { frame.push(epoch); frame.intrinsic(OP_VM_MACHINE_TIME_LOCAL); }
    catch (const RuntimeError &) { denied = true; }
    check(denied, "AOT bypassed localtime capability");
    if (failures == 0) std::cout << "machine localtime hardening passed\n";
    return failures == 0 ? 0 : 1;
}
