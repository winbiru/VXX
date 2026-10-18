#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
int32_t twice(int32_t n);
int32_t sum(int32_t n);
int32_t other(int32_t twice);
#ifdef __cplusplus
}
#endif

int main(void) {
    for (int32_t n = 0; n <= 40; ++n) {
        int32_t expected = 0;
        for (int32_t i = 0; i < n; ++i) {
            if (i == 2) continue;
            if (i == 7) break;
            expected += i * 2;
        }
        if (twice(n) != 2 * n || other(n) != 2 * n || sum(n) != expected) {
            fprintf(stderr, "native self-host mismatch n=%d, sum=%d, expected=%d\n",
                    n, sum(n), expected);
            return 1;
        }
    }
    puts("native-selfhost-host-link-ok");
    return 0;
}
