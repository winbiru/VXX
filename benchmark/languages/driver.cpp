#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Kernel = std::int32_t (*)(std::int32_t, std::int32_t, std::int32_t, std::int32_t);
extern "C" {
void prepare_workload(int);
#ifdef VPP_I32_KERNELS
std::int32_t vpp_arithmetic(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_branch_loop(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_fibonacci(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
#define KERNEL(name) vpp_##name
#else
std::int32_t cpp_arithmetic(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
std::int32_t cpp_branch_loop(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
std::int32_t cpp_fibonacci(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
std::int32_t cpp_list_128(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
std::int32_t cpp_unicode_replace(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
#define KERNEL(name) cpp_##name
#endif
}
std::string setting(const char *key, const char *fallback) {
    const char *value = std::getenv(key);
    return value ? value : fallback;
}
int main() {
    try {
        const auto name = setting("BENCH_CASE", "arithmetic");
        const int iterations = std::stoi(setting("BENCH_ITERATIONS", "16"));
        const int seed = std::stoi(setting("BENCH_SEED", "17"));
        if (iterations < 16 || iterations % 16 || iterations > 100000000) throw std::runtime_error("iterations");
        Kernel fn = nullptr;
        if (name == "arithmetic") fn = KERNEL(arithmetic);
        else if (name == "branch_loop") fn = KERNEL(branch_loop);
        else if (name == "fibonacci") fn = KERNEL(fibonacci);
#ifndef VPP_I32_KERNELS
        else if (name == "list_128") fn = cpp_list_128;
        else if (name == "unicode_replace") fn = cpp_unicode_replace;
#endif
        if (!fn) throw std::runtime_error("unsupported case");
#ifndef VPP_I32_KERNELS
        prepare_workload(seed);
#endif
        std::vector<int> checks;
        double unit = 0;
        for (int i = 0; i < 16; ++i) { int v = fn(seed + i, 0, 0, 0); checks.push_back(v); unit += v % 997; }
        const double expected = unit * (iterations / 16);
        double warm[3]{}, elapsed = 0, checksum = 0;
        for (int batch = 0; batch < 4; ++batch) {
            checksum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < iterations; ++i) checksum += fn(seed + i % 16, 0, 0, 0) % 997;
            elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (checksum != expected || elapsed <= 0) throw std::runtime_error("checksum/clock");
            if (batch < 3) warm[batch] = elapsed;
        }
        std::cout << std::setprecision(17) << "{\"case\":\"" << name << "\",\"iterations\":" << iterations
                  << ",\"seed\":" << seed << ",\"checks\":[";
        for (std::size_t i = 0; i < checks.size(); ++i) std::cout << (i ? "," : "") << checks[i];
        std::cout << "],\"checksum\":\"" << std::fixed << std::setprecision(0) << checksum << "\""
                  << std::defaultfloat << std::setprecision(17) << ",\"elapsed_ms\":" << elapsed
                  << ",\"ns_per_op\":" << elapsed * 1000000 / iterations << ",\"warmup_ms\":["
                  << warm[0] << ',' << warm[1] << ',' << warm[2] << "]}\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
