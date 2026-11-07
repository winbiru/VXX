#include <cstdint>
#include <string>
#include <vector>

namespace {
std::vector<std::string> corpus(16);
}
extern "C" void prepare_workload(int seed) {
    for (int i = 0; i < 16; ++i) {
        const int k = seed + i;
        std::string value;
        for (int j = 0; j < k % 4 + 4; ++j) value += u8"Xin chào Việt Nam 🌿 — Việt! ";
        corpus[k % 16] = value + std::to_string(k);
    }
}
extern "C" std::int32_t cpp_arithmetic(std::int32_t k, std::int32_t, std::int32_t, std::int32_t) {
    return (k + 17) * 31 + (k * 7 + 3) - (k + 5) * 11;
}
extern "C" std::int32_t cpp_branch_loop(std::int32_t k, std::int32_t, std::int32_t, std::int32_t) {
    int x = k * 97 + 11;
    for (int i = 0; i < 256; ++i) { x += 251; if (x >= 65521) x -= 65521; }
    return x;
}
extern "C" std::int32_t cpp_fibonacci(std::int32_t k, std::int32_t, std::int32_t, std::int32_t) {
    int a = 0, b = 1;
    for (int i = 0; i < k % 16 + 20; ++i) { int next = a + b; a = b; b = next; }
    return a;
}
extern "C" std::int32_t cpp_list_128(std::int32_t k, std::int32_t, std::int32_t, std::int32_t) {
    std::vector<int> values;
    for (int i = 0; i < 128; ++i) values.push_back(k + i);
    int sum = 0;
    for (int value : values) if (value % 2 == 0) sum += value;
    return sum;
}
extern "C" std::int32_t cpp_unicode_replace(std::int32_t k, std::int32_t, std::int32_t, std::int32_t) {
    std::string text = corpus[k % 16];
    const std::string before = u8"Việt";
    std::size_t position = 0;
    while ((position = text.find(before, position)) != std::string::npos) {
        text.replace(position, before.size(), "V++");
        position += 3;
    }
    int count = 0;
    for (unsigned char byte : text) if ((byte & 0xc0) != 0x80) ++count;
    return count;
}
