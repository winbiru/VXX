#include "vpp/runtime/time_clock.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "vpp/runtime/error.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#if defined(VPP_FILE_MACHINE_ASM)
#if defined(__APPLE__)
extern "C" int vpp_machine_clock_gettime(int, timespec *) noexcept;
#else
extern "C" std::int64_t vpp_machine_file_syscall4(
    long, std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t, int *) noexcept;
#endif
#endif
#endif

namespace vietvm::runtime {
namespace {

struct ClockSample {
    std::int64_t seconds;
    std::int64_t nanoseconds;
};

bool readClock(bool monotonic, ClockSample &out) noexcept {
#if defined(_WIN32)
    if (monotonic) {
        LARGE_INTEGER counter{}, frequency{};
        if (!::QueryPerformanceCounter(&counter) ||
            !::QueryPerformanceFrequency(&frequency) ||
            frequency.QuadPart <= 0 || counter.QuadPart < 0) return false;
        out.seconds = counter.QuadPart / frequency.QuadPart;
        // Remainder stays bounded even after years of uptime.
        out.nanoseconds = static_cast<std::int64_t>(
            static_cast<long double>(counter.QuadPart % frequency.QuadPart) *
            1000000000.0L / frequency.QuadPart);
        return true;
    }
    FILETIME ft{};
    ::GetSystemTimePreciseAsFileTime(&ft);
    const std::uint64_t windowsTicks =
        (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    // FILETIME epoch = 1601-01-01; units are 100 ns.
    constexpr std::uint64_t unixOffset = 116444736000000000ULL;
    if (windowsTicks > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return false;
    const std::int64_t ticks = static_cast<std::int64_t>(windowsTicks) -
        static_cast<std::int64_t>(unixOffset);
    out.seconds = ticks / 10000000;
    std::int64_t remainder = ticks % 10000000;
    if (remainder < 0) { --out.seconds; remainder += 10000000; }
    out.nanoseconds = remainder * 100;
    return true;
#else
    timespec sample{};
    const int clockId = monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME;
#if defined(VPP_FILE_MACHINE_ASM) && defined(__APPLE__)
    if (vpp_machine_clock_gettime(clockId, &sample) != 0) return false;
#elif defined(VPP_FILE_MACHINE_ASM) && defined(__linux__)
    int osError = 0;
    if (vpp_machine_file_syscall4(
            SYS_clock_gettime, clockId, reinterpret_cast<std::intptr_t>(&sample),
            0, 0, &osError) != 0 || osError != 0) return false;
#else
    // Transitional adapter for POSIX architectures without the machine ASM.
    if (::clock_gettime(clockId, &sample) != 0) return false;
#endif
    static_assert(sizeof(sample.tv_sec) <= sizeof(std::int64_t), "time_t too wide");
    static_assert(sizeof(sample.tv_nsec) <= sizeof(std::int64_t), "nanoseconds too wide");
    out = {static_cast<std::int64_t>(sample.tv_sec),
           static_cast<std::int64_t>(sample.tv_nsec)};
    return true;
#endif
}

void appendI64Le(std::vector<StackValue> &bytes, std::int64_t signedValue) {
    const std::uint64_t bits = static_cast<std::uint64_t>(signedValue);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(make_int_value(static_cast<int>((bits >> shift) & 255u)));
    }
}

} // namespace

bool machineMonotonicMilliseconds(std::int64_t &milliseconds) noexcept {
    ClockSample sample{};
    if (!readClock(true, sample) || sample.seconds < 0 || sample.nanoseconds < 0 ||
        sample.nanoseconds >= 1000000000LL ||
        sample.seconds > (std::numeric_limits<std::int64_t>::max() - 999) / 1000)
        return false;
    milliseconds = sample.seconds * 1000 + sample.nanoseconds / 1000000;
    return true;
}

bool isTimeClockPrimitive(int opcode) noexcept {
    return opcode == OP_VM_MACHINE_TIME_MONOTONIC ||
           opcode == OP_VM_MACHINE_TIME_REALTIME;
}

StackValue executeTimeClockPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (!isTimeClockPrimitive(opcode)) {
        throw RuntimeError("máy đồng hồ: opcode không hợp lệ",
                           RuntimeErrorKind::CallBoundary);
    }
    const bool monotonic = opcode == OP_VM_MACHINE_TIME_MONOTONIC;
    const char *required = monotonic ? "system.time.monotonic" : "system.time.realtime";
    if (capabilities.count(required) == 0) {
        throw RuntimeError(std::string("máy đồng hồ: thiếu quyền ") + required,
                           RuntimeErrorKind::CallBoundary);
    }
    if (!arguments.empty()) {
        throw RuntimeError("máy đồng hồ: không nhận đối số",
                           RuntimeErrorKind::CallBoundary);
    }
    ClockSample sample{};
    if (!readClock(monotonic, sample) || sample.nanoseconds < 0 ||
        sample.nanoseconds >= 1000000000LL || (monotonic && sample.seconds < 0)) {
        throw RuntimeError("máy đồng hồ: OS không trả timespec hợp lệ",
                           RuntimeErrorKind::CallBoundary);
    }
    std::vector<StackValue> bytes;
    bytes.reserve(16);
    appendI64Le(bytes, sample.seconds);
    appendI64Le(bytes, sample.nanoseconds);
    return make_list_value(std::move(bytes));
}

} // namespace vietvm::runtime
