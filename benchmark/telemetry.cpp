#include "telemetry.h"

#include <cstdlib>
#include <new>
#include <stdexcept>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#else
#include <sys/resource.h>
#endif

namespace {
thread_local bool counting = false;
thread_local benchmark::Allocations allocations;

void record(std::size_t bytes) noexcept {
    if (counting) {
        ++allocations.count;
        allocations.bytes += bytes;
    }
}

void *allocate(std::size_t bytes) {
    for (;;) {
        if (void *ptr = std::malloc(bytes == 0 ? 1 : bytes)) {
            record(bytes);
            return ptr;
        }
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}

void *allocateAligned(std::size_t bytes, std::size_t alignment) {
    for (;;) {
#if defined(_WIN32)
        void *ptr = _aligned_malloc(bytes == 0 ? 1 : bytes, alignment);
#else
        void *ptr = nullptr;
        if (posix_memalign(&ptr, alignment, bytes == 0 ? 1 : bytes) != 0) ptr = nullptr;
#endif
        if (ptr) {
            record(bytes);
            return ptr;
        }
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}
void freeAligned(void *ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
} // namespace

// Interposition is linked into the benchmark executable only, never V++ runtime.
void *operator new(std::size_t n) { return allocate(n); }
void *operator new[](std::size_t n) { return allocate(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }
void *operator new(std::size_t n, std::align_val_t a) {
    return allocateAligned(n, static_cast<std::size_t>(a));
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return allocateAligned(n, static_cast<std::size_t>(a));
}
void operator delete(void *p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void *p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void *operator new(std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept {
    try { return allocateAligned(n, static_cast<std::size_t>(a)); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept {
    try { return allocateAligned(n, static_cast<std::size_t>(a)); } catch (...) { return nullptr; }
}
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { freeAligned(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { freeAligned(p); }

namespace benchmark {
void startAllocations() noexcept { allocations = {}; counting = true; }
Allocations stopAllocations() noexcept { counting = false; return allocations; }
std::uint64_t peakRssBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        throw std::runtime_error("GetProcessMemoryInfo failed");
    }
    return counters.PeakWorkingSetSize;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}
} // namespace benchmark
