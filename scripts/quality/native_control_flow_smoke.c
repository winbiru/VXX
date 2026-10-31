#include <stdint.h>
#include <stdio.h>

extern int32_t tinh(int32_t a, int32_t b);
extern int32_t tong(int32_t n);
extern int32_t tong_khi(int32_t n);
extern int32_t dieu_khien_khi(int32_t n);
extern int32_t max_i32(int32_t a, int32_t b);
extern int32_t tong_bon(int32_t a, int32_t b, int32_t c, int32_t d);
extern int32_t tru_dao(int32_t a, int32_t b);
extern int32_t chia_i32(int32_t a, int32_t b);
extern int32_t chia_qua_ham(int32_t a, int32_t b);
extern int32_t phu_dinh(int32_t n);
extern int32_t phan_loai(int32_t a, int32_t b);
extern int32_t tim_dau(int32_t n);
extern int32_t dieu_kien(int32_t n);
extern int32_t bo_qua(int32_t n);
extern int32_t register_limit(int32_t a, int32_t b, int32_t c, int32_t d);
extern int32_t register_overflow(int32_t a, int32_t b, int32_t c, int32_t d);
extern int32_t hoist_vong(int32_t seed, int32_t n);
extern int32_t hoist_hai_vong(int32_t seed, int32_t n);
extern int32_t hoist_long_nhau(int32_t seed, int32_t n);
extern int32_t hoist_goi(int32_t seed, int32_t n);

int main(void) {
    for (int32_t seed = 0; seed < 65521; seed += 4093) {
        for (int32_t n = 0; n <= 100; n += 7) {
            const int32_t expected = (seed + 251 * n) % 65521;
            if (hoist_vong(seed, n) != expected) {
                fprintf(stderr, "native W10 cycle hoisting mismatch: %d %d\n", seed, n);
                return 12;
            }
        }
    }
    for (int32_t seed = -10; seed <= 10; ++seed) {
        for (int32_t n = -1; n <= 20; n += 3) {
            if (hoist_hai_vong(seed, n) != seed ||
                hoist_long_nhau(seed, n) != seed ||
                hoist_goi(seed, n) != seed) {
                fprintf(stderr, "native multi/nested/with-call hoisting mismatch: %d %d\n", seed, n);
                return 13;
            }
        }
    }
    for (int32_t a = -10; a <= 10; ++a) {
        for (int32_t b = -10; b <= 10; ++b) {
            const int32_t expected = a * b + (a < b ? b : -b);
            if (tinh(a, b) != expected || max_i32(a, b) != (a >= b ? a : b) ||
                tru_dao(a, b) != b - a) {
                fprintf(stderr, "native i32 arithmetic/branch mismatch: %d %d\n", a, b);
                return 1;
            }
            const int32_t classified = a < 0 ? (b < 0 ? -2 : -1) : (b == 0 ? 0 : a + b);
            if (phan_loai(a, b) != classified) {
                fprintf(stderr, "native nested early return mismatch: %d %d\n", a, b);
                return 7;
            }
            const int32_t quotient = b == 0 ? -1 : a / b;
            if (chia_i32(a, b) != quotient || chia_qua_ham(a, b) != quotient) {
                fprintf(stderr, "native inline i32 division mismatch: %d %d\n", a, b);
                return 11;
            }
            int32_t values[10] = {a + b, a - b};
            values[2] = values[0] + values[1];
            int32_t sum = values[0] + values[1] + values[2];
            for (int32_t i = 3; i < 10; ++i) {
                values[i] = values[i - 1] + (i % 2 ? i : -i);
                sum += values[i];
            }
            if (register_limit(a, b, a, b) != sum ||
                register_overflow(a, b, a, b) != sum + values[9] + 10) {
                fprintf(stderr, "native register budget/fallback mismatch: %d %d\n", a, b);
                return 9;
            }
        }
    }
    for (int32_t n = 0; n <= 100; ++n) {
        if (tong_bon(n, 5, 2, 4) != n - 3 ||
            phu_dinh(n) != (n != 0)) {
            fprintf(stderr, "native four-argument ABI/unary mismatch: %d\n", n);
            return 6;
        }
        if (tong(n) != n * (n - 1) / 2) {
            fprintf(stderr, "native for-loop mismatch: %d\n", n);
            return 2;
        }
        int32_t whileTotal = 0;
        for (int32_t i = 1; i <= n && i < 6; ++i) {
            if (i != 2) whileTotal += i;
        }
        if (tong_khi(n) != n * (n - 1) / 2 || dieu_khien_khi(n) != whileTotal) {
            fprintf(stderr, "native while-loop mismatch: %d\n", n);
            return 10;
        }
        const int32_t limit = n < 4 ? n : 4;
        const int32_t earlyReturn = limit * (limit - 1) / 2 + (n > 4 ? 100 : 0);
        if (tim_dau(n) != earlyReturn) {
            fprintf(stderr, "native return inside for-loop mismatch: %d\n", n);
            return 8;
        }
        int32_t expected = 0;
        for (int32_t i = 0; i < n; ++i) {
            if (i == 2) continue;
            if (i == 6) break;
            expected += i;
        }
        if (bo_qua(n) != expected) {
            fprintf(stderr, "native break/continue mismatch: %d\n", n);
            return 3;
        }
        if (dieu_kien(n) != (n != 0 ? 12 : -12)) {
            fprintf(stderr, "native conditional mismatch: %d\n", n);
            return 4;
        }
    }
    if (dieu_kien(-1) != 12 || phu_dinh(-1) != 1 ||
        max_i32(INT32_MIN, INT32_MAX) != INT32_MAX ||
        max_i32(INT32_MIN, INT32_MIN) != INT32_MIN ||
        chia_i32(INT32_MIN, -1) != INT32_MIN ||
        chia_qua_ham(INT32_MIN, -1) != INT32_MIN ||
        chia_i32(INT32_MIN, 0) != -1) {
        fprintf(stderr, "native signed i32 boundary mismatch\n");
        return 5;
    }
    puts("native_control_flow_ok");
    return 0;
}
