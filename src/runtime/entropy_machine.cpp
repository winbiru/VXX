#include "vpp/runtime/entropy_machine.h"

#include <array>
#include <cstdint>
#include <utility>

#include "vpp/runtime/error.h"

#if defined(_WIN32)
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <unistd.h>
#if defined(VPP_FILE_MACHINE_ASM)
extern "C" int vpp_machine_entropy_getentropy(void *, std::size_t) noexcept;
#endif
#elif defined(__linux__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#if defined(VPP_FILE_MACHINE_ASM)
extern "C" std::int64_t vpp_machine_file_syscall4(
    long, std::intptr_t, std::intptr_t, std::intptr_t, std::intptr_t, int *) noexcept;
#endif
#endif

namespace vietvm::runtime {
namespace {

bool osEntropy(std::uint8_t *bytes, std::size_t count) noexcept {
#if defined(_WIN32)
    // Windows CNG owns the system cryptographic random source.
    return BCRYPT_SUCCESS(::BCryptGenRandom(
        nullptr, bytes, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(__APPLE__) && defined(VPP_FILE_MACHINE_ASM)
    // Documented Darwin libSystem ABI; Assembly is the only OS bridge.
    return vpp_machine_entropy_getentropy(bytes, count) == 0;
#elif defined(__APPLE__)
    // Compatibility path for Apple architectures without the Assembly adapter.
    return ::getentropy(bytes, count) == 0;
#elif defined(__linux__) && defined(VPP_FILE_MACHINE_ASM)
    std::size_t filled = 0;
    while (filled < count) {
        int osError = 0;
        const auto got = vpp_machine_file_syscall4(
            SYS_getrandom, reinterpret_cast<std::intptr_t>(bytes + filled),
            static_cast<std::intptr_t>(count - filled), 0, 0, &osError);
        if (got < 0) {
            if (osError == EINTR) continue;
            return false;
        }
        if (got == 0 || static_cast<std::size_t>(got) > count - filled) return false;
        filled += static_cast<std::size_t>(got);
    }
    return true;
#elif defined(__linux__)
    // Compatibility path for Linux architectures without the Assembly adapter.
    std::size_t filled = 0;
    while (filled < count) {
        const auto got = ::syscall(SYS_getrandom, bytes + filled, count - filled, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (got == 0 || static_cast<std::size_t>(got) > count - filled) return false;
        filled += static_cast<std::size_t>(got);
    }
    return true;
#else
    (void)bytes;
    (void)count;
    return false;
#endif
}

} // namespace

bool isEntropyMachinePrimitive(int opcode) noexcept {
    return opcode == OP_VM_MACHINE_ENTROPY_READ;
}

StackValue executeEntropyMachinePrimitive(
    Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities) {
    if (!isEntropyMachinePrimitive(opcode)) {
        throw RuntimeError("máy entropy: opcode không hợp lệ", RuntimeErrorKind::CallBoundary);
    }
    if (capabilities.count("system.entropy.read") == 0) {
        throw RuntimeError("máy entropy: thiếu quyền system.entropy.read",
                           RuntimeErrorKind::CallBoundary);
    }
    if (arguments.size() != 1) {
        throw RuntimeError("máy entropy: cần đúng một đối số", RuntimeErrorKind::CallBoundary);
    }
    const auto *length = std::get_if<int>(&arguments[0]);
    if (!length || *length < 1 || *length > 256) {
        throw RuntimeError("máy entropy: số byte phải là số nguyên từ 1 đến 256",
                           RuntimeErrorKind::CallBoundary);
    }
    std::array<std::uint8_t, 256> data{};
    if (!osEntropy(data.data(), static_cast<std::size_t>(*length))) {
        throw RuntimeError("máy entropy: OS không cấp entropy bảo mật",
                           RuntimeErrorKind::CallBoundary);
    }
    std::vector<StackValue> result;
    result.reserve(static_cast<std::size_t>(*length));
    for (int i = 0; i < *length; ++i) result.push_back(make_int_value(data[i]));
    return make_list_value(std::move(result));
}

} // namespace vietvm::runtime
