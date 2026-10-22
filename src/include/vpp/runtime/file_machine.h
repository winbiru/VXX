#pragma once

#include <cstddef>
#include <cstdint>

namespace vietvm::runtime::file_machine {

// Raw syscall result: no libc errno storage, buffering or retry policy.
struct Result {
    std::int64_t value;
    int error;
};
Result open(const char *path, int flags, unsigned mode) noexcept;
Result read(int descriptor, void *buffer, std::size_t count) noexcept;
Result write(int descriptor, const void *buffer, std::size_t count) noexcept;
Result seek(int descriptor, std::int64_t offset, int origin) noexcept;
Result close(int descriptor) noexcept;
Result remove(const char *path) noexcept;
Result mkdir(const char *path, unsigned mode) noexcept;
Result rmdir(const char *path) noexcept;
#if defined(__linux__)
// Linux directory entries use getdents64 through the same syscall-only ASM ABI.
Result directoryEntries(int descriptor, void *buffer, std::size_t count) noexcept;
#endif

} // namespace vietvm::runtime::file_machine
