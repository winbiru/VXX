// C ABI smoke for native objects generated from V++ leaf functions.
// Compile and link this file for the *target* CPU, then execute under QEMU.
#include <stdint.h>

extern int tra42(void);
extern int tra_am(void);
extern int tra_i32(void);
extern int tra_i32_am(void);
extern int tra_i32_am_ngoac(void);
extern int tinh_hang(void);
extern int tinh_am(void);
extern int tinh_sau(void);
extern int cong(int a, int b);
extern int cong_dao(int a, int b);
extern int tru(int a, int b);

int main(void) {
    if (tra42() != 42) return 1;
    if (tra_am() != -7) return 2;
    if (tra_i32() != INT32_MAX) return 3;
    if (cong(12, 30) != 42) return 4;
    if (cong(-120, 12) != -108) return 5;
    if (cong(-1, 1) != 0) return 6;
    if (cong(INT32_MIN + 1, -1) != INT32_MIN) return 7;
    if (cong(INT32_MAX - 7, 7) != INT32_MAX) return 8;
    if (tru(30, 12) != 18) return 9;
    if (tru(12, 30) != -18) return 10;
    if (tru(-100, -20) != -80) return 11;
    if (tru(INT32_MIN + 1, 1) != INT32_MIN) return 12;
    if (tru(INT32_MAX, 7) != INT32_MAX - 7) return 13;
    if (tinh_hang() != 48) return 14;
    if (tinh_am() != -7) return 15;
    if (tinh_sau() != 86) return 16;
    if (cong_dao(-120, 12) != -108) return 17;
    if (tra_i32_am() != INT32_MIN) return 18;
    if (tra_i32_am_ngoac() != INT32_MIN) return 19;
    return 0;
}
