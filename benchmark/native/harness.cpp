#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#define DECLARE_BACKEND(prefix) \
    extern "C" std::int32_t prefix##_arithmetic(std::int32_t, std::int32_t, std::int32_t, std::int32_t); \
    extern "C" std::int32_t prefix##_cycle(std::int32_t, std::int32_t, std::int32_t, std::int32_t); \
    extern "C" std::int32_t prefix##_fibonacci(std::int32_t, std::int32_t, std::int32_t, std::int32_t)
DECLARE_BACKEND(cpp_o0);
DECLARE_BACKEND(cpp_o2);
DECLARE_BACKEND(cpp_o3);
DECLARE_BACKEND(vpp);

using Kernel = std::int32_t (*)(std::int32_t, std::int32_t, std::int32_t, std::int32_t);
using Input = std::array<std::int32_t, 4>;
constexpr std::size_t inputCount = 4096;
static volatile std::uint64_t consumed = 0;

struct Sample {
    double seconds;
    std::uint64_t checksum;
};

// One identical indirect-call harness for every backend. Kernels live in
// separate object files; no LTO or interprocedural inlining is enabled.
__attribute__((noinline)) static Sample measure(
    Kernel function, const std::array<Input, inputCount> &inputs, std::uint64_t calls) {
    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < calls; ++i) {
        const Input &in = inputs[i & (inputCount - 1)];
        checksum += static_cast<std::uint32_t>(function(in[0], in[1], in[2], in[3]));
    }
    const auto end = std::chrono::steady_clock::now();
    consumed = checksum;
    return {std::chrono::duration<double>(end - start).count(), checksum};
}

static std::int32_t reference(int workload, const Input &in) {
    if (workload == 0) return (in[0] + in[1]) - in[2] * in[3];
    if (workload == 1)
        return static_cast<std::int32_t>((static_cast<std::int64_t>(in[0]) + 251LL * in[1]) % 65521);
    std::int64_t a = 0, b = 1;
    for (std::int32_t i = 0; i < in[0]; ++i) {
        const auto next = a + b;
        a = b;
        b = next;
    }
    return static_cast<std::int32_t>(a);
}

static double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

int main(int argc, char **argv) {
    try {
        const int rounds = argc > 1 ? std::stoi(argv[1]) : 7;
        const double targetSeconds = argc > 2 ? std::stod(argv[2]) : 0.03;
        if (rounds < 3 || rounds % 2 == 0 || targetSeconds <= 0)
            throw std::runtime_error("rounds must be odd and >= 3; target seconds must be positive");
        const std::array<const char *, 4> backendNames = {"cpp_O0", "cpp_O2", "cpp_O3", "vpp_native"};
        const std::array<const char *, 3> workloadNames = {"arithmetic", "cycle", "fibonacci"};
        const std::array<std::array<Kernel, 4>, 3> functions = {{
            {cpp_o0_arithmetic, cpp_o2_arithmetic, cpp_o3_arithmetic, vpp_arithmetic},
            {cpp_o0_cycle, cpp_o2_cycle, cpp_o3_cycle, vpp_cycle},
            {cpp_o0_fibonacci, cpp_o2_fibonacci, cpp_o3_fibonacci, vpp_fibonacci}
        }};
        std::mt19937 random(20261016);
        std::mt19937 orderRandom(20261017);
        std::cout << std::setprecision(12) << "{\"rounds\":" << rounds
                  << ",\"target_seconds\":" << targetSeconds
                  << ",\"input_count\":" << inputCount << ",\"workloads\":[";
        for (int workload = 0; workload < 3; ++workload) {
            std::array<Input, inputCount> inputs;
            std::uint64_t blockChecksum = 0;
            for (Input &in : inputs) {
                if (workload == 0) {
                    for (auto &value : in) value = static_cast<std::int32_t>(random() % 2001) - 1000;
                } else if (workload == 1) {
                    in = {static_cast<std::int32_t>(random() % 65521),
                          static_cast<std::int32_t>(128 + random() % 128), 0, 0};
                } else {
                    in = {static_cast<std::int32_t>(20 + random() % 20), 0, 0, 0};
                }
                const auto expected = reference(workload, in);
                blockChecksum += static_cast<std::uint32_t>(expected);
                for (Kernel function : functions[workload]) {
                    if (function(in[0], in[1], in[2], in[3]) != expected)
                        throw std::runtime_error(std::string("correctness mismatch: ") + workloadNames[workload]);
                }
            }
            // Warm each implementation, then calibrate once against C++ -O3.
            for (Kernel function : functions[workload]) measure(function, inputs, inputCount * 4);
            std::uint64_t calls = inputCount * 4;
            while (calls < (1ULL << 26) && measure(functions[workload][2], inputs, calls).seconds < targetSeconds)
                calls *= 2;
            const std::uint64_t expectedChecksum = blockChecksum * (calls / inputCount);
            std::array<std::vector<double>, 4> samples;
            std::vector<std::array<int, 4>> orders;
            for (int round = 0; round < rounds; ++round) {
                std::array<int, 4> order = {0, 1, 2, 3};
                std::shuffle(order.begin(), order.end(), orderRandom);
                orders.push_back(order);
                for (int backend : order) {
                    const Sample sample = measure(functions[workload][backend], inputs, calls);
                    if (sample.checksum != expectedChecksum)
                        throw std::runtime_error("timed checksum mismatch");
                    samples[backend].push_back(sample.seconds * 1e9 / static_cast<double>(calls));
                }
            }
            if (workload) std::cout << ',';
            std::cout << "{\"name\":\"" << workloadNames[workload] << "\",\"calls_per_sample\":" << calls
                      << ",\"checksum\":" << expectedChecksum << ",\"backends\":[";
            for (int backend = 0; backend < 4; ++backend) {
                if (backend) std::cout << ',';
                const auto &values = samples[backend];
                std::cout << "{\"name\":\"" << backendNames[backend]
                          << "\",\"median_ns_per_call\":" << median(values)
                          << ",\"min_ns_per_call\":" << *std::min_element(values.begin(), values.end())
                          << ",\"max_ns_per_call\":" << *std::max_element(values.begin(), values.end())
                          << ",\"samples_ns_per_call\":[";
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (i) std::cout << ',';
                    std::cout << values[i];
                }
                std::cout << "]}";
            }
            std::cout << "],\"run_orders\":[";
            for (std::size_t r = 0; r < orders.size(); ++r) {
                if (r) std::cout << ',';
                std::cout << '[';
                for (int i = 0; i < 4; ++i) {
                    if (i) std::cout << ',';
                    std::cout << orders[r][i];
                }
                std::cout << ']';
            }
            std::cout << "]}" << std::flush;
        }
        std::cout << "],\"correctness\":\"PASS\",\"checksum_sink\":" << consumed << "}\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
