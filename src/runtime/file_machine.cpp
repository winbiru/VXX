#include "vpp/runtime/file_machine.h"

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vietvm::runtime::file_machine {
namespace {

// Follow the existing scripts/ASM boundary used by native V++ calls.
// This adapter contains no inline assembler or foreign symbol resolution.
#if defined(VPP_FILE_MACHINE_ASM)
extern "C" std::int64_t vpp_machine_file_syscall4(long number,
    std::intptr_t a, std::intptr_t b, std::intptr_t c, std::intptr_t d, int *error) noexcept;
Result invoke(long number, std::intptr_t a = 0, std::intptr_t b = 0,
              std::intptr_t c = 0, std::intptr_t d = 0) noexcept {
    int error = 0;
    const auto value = vpp_machine_file_syscall4(number, a, b, c, d, &error);
    return {value, error};
}
#else
// Other existing POSIX targets retain the OS ABI adapter until their ASM
// calling convention is implemented and verified on a runner for that CPU.
Result fromPosix(std::int64_t value) noexcept {
    return {value, value < 0 ? errno : 0};
}
#endif

} // namespace

Result open(const char *path, int flags, unsigned mode) noexcept {
#if defined(VPP_FILE_MACHINE_ASM) && defined(__linux__)
    return invoke(SYS_openat, AT_FDCWD, reinterpret_cast<std::intptr_t>(path), flags, mode);
#elif defined(VPP_FILE_MACHINE_ASM) && defined(__APPLE__)
    return invoke(SYS_open, reinterpret_cast<std::intptr_t>(path), flags, mode);
#else
    return fromPosix(::open(path, flags, mode));
#endif
}
Result read(int fd, void *buffer, std::size_t count) noexcept {
#if defined(VPP_FILE_MACHINE_ASM)
    return invoke(SYS_read, fd, reinterpret_cast<std::intptr_t>(buffer), count);
#else
    return fromPosix(::read(fd, buffer, count));
#endif
}
Result write(int fd, const void *buffer, std::size_t count) noexcept {
#if defined(VPP_FILE_MACHINE_ASM)
    return invoke(SYS_write, fd, reinterpret_cast<std::intptr_t>(buffer), count);
#else
    return fromPosix(::write(fd, buffer, count));
#endif
}
Result seek(int fd, std::int64_t offset, int origin) noexcept {
#if defined(VPP_FILE_MACHINE_ASM)
    return invoke(SYS_lseek, fd, offset, origin);
#else
    if (offset != static_cast<std::int64_t>(static_cast<off_t>(offset))) return {-1, EOVERFLOW};
    return fromPosix(::lseek(fd, static_cast<off_t>(offset), origin));
#endif
}
Result close(int fd) noexcept {
#if defined(VPP_FILE_MACHINE_ASM)
    return invoke(SYS_close, fd);
#else
    return fromPosix(::close(fd));
#endif
}

Result remove(const char *path) noexcept {
#if defined(VPP_FILE_MACHINE_ASM) && defined(__linux__)
    return invoke(SYS_unlinkat, AT_FDCWD, reinterpret_cast<std::intptr_t>(path), 0);
#elif defined(VPP_FILE_MACHINE_ASM) && defined(__APPLE__)
    return invoke(SYS_unlink, reinterpret_cast<std::intptr_t>(path));
#else
    return fromPosix(::unlink(path));
#endif
}

Result mkdir(const char *path, unsigned mode) noexcept {
#if defined(VPP_FILE_MACHINE_ASM) && defined(__linux__)
    return invoke(SYS_mkdirat, AT_FDCWD, reinterpret_cast<std::intptr_t>(path), mode);
#elif defined(VPP_FILE_MACHINE_ASM) && defined(__APPLE__)
    return invoke(SYS_mkdir, reinterpret_cast<std::intptr_t>(path), mode);
#else
    return fromPosix(::mkdir(path, mode));
#endif
}
Result rmdir(const char *path) noexcept {
#if defined(VPP_FILE_MACHINE_ASM) && defined(__linux__)
    return invoke(SYS_unlinkat, AT_FDCWD, reinterpret_cast<std::intptr_t>(path), AT_REMOVEDIR);
#elif defined(VPP_FILE_MACHINE_ASM) && defined(__APPLE__)
    return invoke(SYS_rmdir, reinterpret_cast<std::intptr_t>(path));
#else
    return fromPosix(::rmdir(path));
#endif
}
#if defined(__linux__)
Result directoryEntries(int fd, void *buffer, std::size_t count) noexcept {
#if defined(VPP_FILE_MACHINE_ASM)
    return invoke(SYS_getdents64, fd, reinterpret_cast<std::intptr_t>(buffer), count);
#else
    return fromPosix(::syscall(SYS_getdents64, fd, buffer, count));
#endif
}
#endif

} // namespace vietvm::runtime::file_machine
#endif
