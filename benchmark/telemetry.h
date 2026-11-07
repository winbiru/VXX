#pragma once

#include <cstddef>
#include <cstdint>

namespace benchmark {
// Counts successful C++ new/new[] requests on the measuring thread only.
// C malloc, OS mappings and allocator overhead are not included.
struct Allocations {
    std::uint64_t count = 0;
    std::uint64_t bytes = 0;
};
void startAllocations() noexcept;
Allocations stopAllocations() noexcept;
std::uint64_t peakRssBytes();
} // namespace benchmark
