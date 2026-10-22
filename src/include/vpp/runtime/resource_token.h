#pragma once
#include <atomic>
#include <cstdint>

namespace vietvm::runtime {
// Shared by machine primitives and FFI so handles cannot alias across providers.
inline std::uint64_t nextOpaqueResourceToken() noexcept {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}
}
