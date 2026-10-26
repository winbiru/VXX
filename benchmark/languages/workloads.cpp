#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<std::string> texts;
static int arithmetic(int i) { return (1000 + i * 7 + 20 + i) - (i % 7) * (i % 11); }
static int branch(int i) {
    int x = i * 4093 + 7;
    for (int j = 0; j < 128 + i; ++j) { x += 251; if (x >= 65521) x -= 65521; }
    return x;
}
static int fibonacci(int i) {
    int a = 0, b = 1;
    for (int j = 0; j < 20 + i; ++j) { const int next = a + b; a = b; b = next; }
    return a;
}
static int list(int i) {
    std::vector<int> values;
    for (int j = 0; j < 128; ++j) values.push_back((j * 17 + i) % 1000);
    int result = 0;
    for (int j = 0; j < 128; ++j) { const int value = values[j]; if (value >= 500) result += value; }
    return result;
}
static int text(int i) {
    std::string result = texts[i];
    const std::string from = u8"Việt", to = "V++";
    for (std::size_t p = 0; (p = result.find(from, p)) != std::string::npos; p += to.size()) {
        result.replace(p, from.size(), to);
    }
    int count = 0;
    for (unsigned char byte : result) if ((byte & 0xc0) != 0x80) ++count;
    return count;
}
int main() {
    const std::string name = std::getenv("VPP_BENCH_CASE");
    const int iterations = std::stoi(std::getenv("VPP_BENCH_ITERS"));
    std::string fixture;
    for (int i = 0; i < 16; ++i) { fixture += u8"Xin chào Việt Nam! "; texts.push_back(fixture); }
    int (*work)(int) = nullptr;
    if (name == "arithmetic") work = arithmetic;
    if (name == "branch") work = branch;
    if (name == "fibonacci") work = fibonacci;
    if (name == "list") work = list;
    if (name == "text") work = text;
    if (!work) throw std::runtime_error("unknown workload");
    for (int i = 0; i < 16; ++i) std::cout << work(i) << '\n';
    double elapsed = 0, checksum = 0, previous = -1;
    for (int batch = 0; batch < 4; ++batch) {
        checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) checksum += work(i % 16);
        elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (previous >= 0 && previous != checksum) throw std::runtime_error("unstable checksum");
        previous = checksum;
    }
    std::cout << std::setprecision(17) << elapsed << '\n' << checksum << '\n' << iterations << '\n';
}
