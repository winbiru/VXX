#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int32_t bit_va(int32_t, int32_t);
extern int32_t bit_hoac(int32_t, int32_t);
extern int32_t bit_xor(int32_t, int32_t);
extern int32_t dao_bit(int32_t);
extern int32_t dich_trai(int32_t, int32_t);
extern int32_t dich_phai(int32_t, int32_t);
extern int32_t gan_bit(int32_t, int32_t);
extern int32_t uu_tien_bit(int32_t, int32_t);
extern int32_t bit_khung(int32_t, int32_t);

static int32_t signed_bits(uint32_t bits) {
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

// Oracle dùng chia có làm tròn xuống, độc lập với lệnh ASR của backend.
static int32_t shift_right(int32_t a, int32_t b) {
    const int64_t divisor = INT64_C(1) << ((uint32_t)b & 31u);
    return (int32_t)(a >= 0 ? a / divisor : -((-(int64_t)a + divisor - 1) / divisor));
}
static int32_t shift_left(int32_t a, int32_t b) {
    return signed_bits((uint32_t)a << ((uint32_t)b & 31u));
}

int main(void) {
    const int32_t values[] = {INT32_MIN, INT32_MIN + 1, -129, -33, -32, -31,
        -16, -1, 0, 1, 2, 7, 13, 30, 31, 32, 33, 129, INT32_MAX};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        const int32_t a = values[i];
        for (unsigned j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
            const int32_t b = values[j];
            const int32_t expected_assignment = shift_right(shift_left(((a & b) | 129) ^ 33, b), b);
            const int32_t q = signed_bits((uint32_t)a + 64u);
            const int32_t p = signed_bits((uint32_t)b + 56u);
            const int32_t frame = ((~a ^ b) & q) | shift_right(shift_left(p, b), a & 31);
            const uint32_t precedence = (uint32_t)(a | (b & 3)) + (uint32_t)(a ^ (b & 7)) +
                (uint32_t)shift_left(a, signed_bits((uint32_t)b + 1u)) +
                (uint32_t)~a + 2u + (uint32_t)(a == (b < 3));
            if (bit_va(a, b) != (a & b) || bit_hoac(a, b) != (a | b) ||
                bit_xor(a, b) != (a ^ b) || dao_bit(a) != ~a ||
                dich_trai(a, b) != shift_left(a, b) || dich_phai(a, b) != shift_right(a, b) ||
                gan_bit(a, b) != expected_assignment || bit_khung(a, b) != frame ||
                uu_tien_bit(a, b) != signed_bits(precedence)) {
                fprintf(stderr, "Sai phép bit i32: a=%d b=%d\n", a, b);
                return 1;
            }
        }
    }
    puts("Toán tử bit native đúng");
    return 0;
}
