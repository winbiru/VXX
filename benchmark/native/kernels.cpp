#include <cstdint>

#ifndef BENCH_PREFIX
#error "Compile with -DBENCH_PREFIX=cpp_o0, cpp_o2 or cpp_o3"
#endif
#define BENCH_JOIN_INNER(a, b) a##b
#define BENCH_JOIN(a, b) BENCH_JOIN_INNER(a, b)
#define BENCH_NAME(name) BENCH_JOIN(BENCH_PREFIX, name)

extern "C" std::int32_t BENCH_NAME(_arithmetic)(
    std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) {
    return (a + b) - c * d;
}

extern "C" std::int32_t BENCH_NAME(_cycle)(
    std::int32_t seed, std::int32_t n, std::int32_t, std::int32_t) {
    std::int32_t x = seed;
    for (std::int32_t i = 0; i < n; i = i + 1) {
        x = x + 251;
        if (x >= 65521) x = x - 65521;
    }
    return x;
}

extern "C" std::int32_t BENCH_NAME(_fibonacci)(
    std::int32_t n, std::int32_t, std::int32_t, std::int32_t) {
    std::int32_t a = 0;
    std::int32_t b = 1;
    for (std::int32_t i = 0; i < n; i = i + 1) {
        const std::int32_t next = a + b;
        a = b;
        b = next;
    }
    return a;
}
