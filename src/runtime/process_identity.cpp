#include "vpp/runtime/process_identity.h"

#include <climits>
#include <cstdint>
#include <string>

#include "vpp/runtime/error.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && defined(VPP_FILE_MACHINE_ASM)
extern "C" std::int64_t vpp_machine_file_syscall4(
    long, std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t, int *) noexcept;
#endif

namespace vietvm::runtime {
namespace {

struct IdentityOperation {
    const char *capability;
    unsigned index;
    bool processId;
};

IdentityOperation operationFor(Opcode opcode) {
    switch (opcode) {
        case OP_VM_MACHINE_PROCESS_PID:  return {"system.process.id", 0, true};
        case OP_VM_MACHINE_PROCESS_PPID: return {"system.process.parent_id", 1, true};
        case OP_VM_MACHINE_PROCESS_UID:  return {"system.user.id", 2, false};
        case OP_VM_MACHINE_PROCESS_EUID: return {"system.user.effective_id", 3, false};
        case OP_VM_MACHINE_PROCESS_GID:  return {"system.group.id", 4, false};
        case OP_VM_MACHINE_PROCESS_EGID: return {"system.group.effective_id", 5, false};
        default: throw RuntimeError("máy tiến trình: opcode không hợp lệ",
                                    RuntimeErrorKind::CallBoundary);
    }
}

// No stdio, malloc, or dynamic symbol lookup crosses the supported POSIX
// machine boundary: the same ASM syscall entry used by the file machine.
std::int64_t readIdentity(unsigned index) {
#if defined(_WIN32)
    if (index == 0) return static_cast<std::int64_t>(GetCurrentProcessId());
    if (index == 1) {
        const DWORD self = GetCurrentProcessId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return -1;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        std::int64_t parent = -1;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == self) {
                    parent = static_cast<std::int64_t>(entry.th32ParentProcessID);
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return parent;
    }
    return -1;  // Windows has no POSIX uid/gid API.
#else
    long number;
    switch (index) {
        case 0: number = SYS_getpid; break;
        case 1: number = SYS_getppid; break;
        case 2: number = SYS_getuid; break;
        case 3: number = SYS_geteuid; break;
        case 4: number = SYS_getgid; break;
        case 5: number = SYS_getegid; break;
        default: return -1;
    }
#if defined(VPP_FILE_MACHINE_ASM)
    int osError = 0;
    return vpp_machine_file_syscall4(number, 0, 0, 0, 0, &osError);
#else
    // Unsupported CPU families retain a provisional POSIX adapter.
    return static_cast<std::int64_t>(::syscall(number));
#endif
#endif
}

} // namespace

bool isProcessIdentityPrimitive(int opcode) noexcept {
    return opcode >= OP_VM_MACHINE_PROCESS_PID && opcode <= OP_VM_MACHINE_PROCESS_EGID;
}

StackValue executeProcessIdentityPrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    const auto operation = operationFor(opcode);
    if (capabilities.find(operation.capability) == capabilities.end()) {
        throw RuntimeError("máy tiến trình: thiếu quyền " + std::string(operation.capability),
                           RuntimeErrorKind::CallBoundary);
    }
    if (!arguments.empty()) {
        throw RuntimeError("máy tiến trình: primitive không nhận đối số",
                           RuntimeErrorKind::CallBoundary);
    }
#if defined(_WIN32)
    if (!operation.processId) {
        throw RuntimeError("máy tiến trình: UID/GID Windows chưa được hỗ trợ",
                           RuntimeErrorKind::CallBoundary);
    }
#endif
    const std::int64_t value = readIdentity(operation.index);
    if (value < 0 || value > UINT32_MAX ||
        (operation.processId && value > INT32_MAX)) {
        throw RuntimeError("máy tiến trình: giá trị ID không hợp lệ hoặc syscall thất bại",
                           RuntimeErrorKind::CallBoundary);
    }
    if (value <= INT32_MAX) return make_int_value(static_cast<int>(value));
    return make_abi_integer_value(AbiInteger::unsignedNumber(static_cast<std::uint64_t>(value)));
}

} // namespace vietvm::runtime
