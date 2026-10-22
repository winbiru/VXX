#include "vpp/runtime/time_sleep.h"

#include <cerrno>
#include <cstdint>

#include "vpp/runtime/error.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/syscall.h>
#include <time.h>
#if defined(__APPLE__)
#include <poll.h>
#endif
#if defined(VPP_FILE_MACHINE_ASM)
extern "C" std::int64_t vpp_machine_file_syscall4(
    long, std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t, int *) noexcept;
#endif
static_assert(EINTR == 4, "The .vi sleep retry contract requires EINTR = 4");
#endif

namespace vietvm::runtime {
namespace {

// Chờ đúng một đợt. Trả errno dương trong cùng lần gọi, không đọc errno
// riêng sau khi mã V++ đã chạy tiếp. Darwin không có SYS_nanosleep:
// poll(nfds=0,timeout) dùng BSD syscall trực tiếp ở ranh giới ASM.
int sleepOnce(unsigned milliseconds) noexcept {
#if defined(_WIN32)
    ::Sleep(static_cast<DWORD>(milliseconds));
    return 0;
#elif defined(VPP_FILE_MACHINE_ASM)
    int osError = 0;
#if defined(__APPLE__)
    (void)vpp_machine_file_syscall4(SYS_poll, 0, 0, milliseconds, 0, &osError);
#elif defined(__linux__)
    const timespec requested{
        static_cast<time_t>(milliseconds / 1000),
        static_cast<long>(milliseconds % 1000) * 1000000L};
    (void)vpp_machine_file_syscall4(
        SYS_nanosleep, reinterpret_cast<std::intptr_t>(&requested), 0, 0, 0, &osError);
#else
#error Unsupported ASM machine sleep ABI
#endif
    return osError;
#elif defined(__APPLE__)
    // Compatibility for unsupported POSIX CPU targets only.
    return ::poll(nullptr, 0, static_cast<int>(milliseconds)) == 0 ? 0 : errno;
#else
    const timespec requested{
        static_cast<time_t>(milliseconds / 1000),
        static_cast<long>(milliseconds % 1000) * 1000000L};
    return ::nanosleep(&requested, nullptr) == 0 ? 0 : errno;
#endif
}

} // namespace

bool isTimeSleepPrimitive(int opcode) noexcept {
    return opcode == OP_VM_MACHINE_TIME_SLEEP;
}

StackValue executeTimeSleepPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (!isTimeSleepPrimitive(opcode)) {
        throw RuntimeError("máy ngủ: opcode không hợp lệ", RuntimeErrorKind::CallBoundary);
    }
    if (capabilities.count("system.time.sleep") == 0) {
        throw RuntimeError("máy ngủ: thiếu quyền system.time.sleep",
                           RuntimeErrorKind::CallBoundary);
    }
    if (arguments.size() != 1) {
        throw RuntimeError("máy ngủ: cần đúng một đối số mili giây",
                           RuntimeErrorKind::CallBoundary);
    }
    const auto *duration = std::get_if<int>(&arguments[0]);
    if (duration == nullptr || *duration < 0 || *duration > 999) {
        throw RuntimeError("máy ngủ: mili giây phải là i32 trong khoảng 0..999",
                           RuntimeErrorKind::CallBoundary);
    }
    return make_int_value(*duration == 0 ? 0 : sleepOnce(static_cast<unsigned>(*duration)));
}

} // namespace vietvm::runtime
