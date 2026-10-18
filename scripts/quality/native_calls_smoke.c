#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
extern int32_t co_so(void);
extern int32_t them(int32_t, int32_t);
extern int32_t nhan_sau(int32_t);
extern int32_t ket_hop(int32_t, int32_t, int32_t, int32_t);
extern int32_t tinh_vong(int32_t);
extern int32_t goi_bon(int32_t, int32_t, int32_t, int32_t);
extern int32_t tong_muoi(int32_t, int32_t, int32_t, int32_t, int32_t,
                        int32_t, int32_t, int32_t, int32_t, int32_t);
extern int32_t goi_muoi(int32_t, int32_t, int32_t, int32_t, int32_t,
                       int32_t, int32_t, int32_t, int32_t, int32_t);
#ifdef __cplusplus
}
#endif

int main(void) {
    const int direct_ten = tong_muoi(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    const int nested_ten = goi_muoi(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    const int signed_ten = goi_muoi(-1, 2, -3, 4, -5, 6, -7, 8, -9, 10);
    if (co_so() != 7 || them(-5, 12) != 7 || nhan_sau(0) != 1 ||
        nhan_sau(5) != 120 || nhan_sau(7) != 5040 ||
        ket_hop(1, 2, 3, 4) != 17 || ket_hop(-4, 10, -5, 4) != 12 ||
        tinh_vong(0) != 0 || tinh_vong(4) != 34 ||
        goi_bon(4, 5, 6, 7) != 22 || goi_bon(-5, 2, -3, 8) != 2 ||
        direct_ten != 55 || nested_ten != 55 || signed_ten != 5) {
        fprintf(stderr, "native_calls_mismatch: direct_10=%d nested_10=%d signed_10=%d\n",
                direct_ten, nested_ten, signed_ten);
        return 1;
    }
    puts("native_calls_ok");
    return 0;
}
