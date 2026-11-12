#include <stdint.h>

// Biên dịch riêng, không LTO: bộ đo không thể gập hàm theo đầu vào đã biết.
// Các số trong bộ đo không tràn; -fwrapv giữ cùng quy tắc i32 với V++.
int32_t native_arithmetic(int32_t k, int32_t p1, int32_t p2, int32_t p3) {
    (void)p1; (void)p2; (void)p3;
    return (k + 17) * 31 + (k * 7 + 3) - (k + 5) * 11;
}
int32_t native_branch_loop(int32_t k, int32_t p1, int32_t p2, int32_t p3) {
    (void)p1; (void)p2; (void)p3;
    int32_t x = k * 97 + 11;
    for (int i = 0; i < 256; ++i) { x += 251; if (x >= 65521) x -= 65521; }
    return x;
}
int32_t native_fibonacci(int32_t k, int32_t p1, int32_t p2, int32_t p3) {
    (void)p1; (void)p2; (void)p3;
    int32_t a = 0, b = 1;
    for (int i = 0; i < k % 16 + 20; ++i) { int32_t next = a + b; a = b; b = next; }
    return a;
}
