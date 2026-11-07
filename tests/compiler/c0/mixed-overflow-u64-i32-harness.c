#include <stdint.h>
#include <limits.h>

extern int gnu_mixed_forward(unsigned long, int, int *);
extern int gnu_mixed_reverse(int, unsigned long, int *);
extern int minic_mixed_forward(unsigned long, int, int *);
extern int minic_mixed_reverse(int, unsigned long, int *);

int main(void) {
    static const unsigned long large_values[] = {
        0UL, 1UL, 0x7fffffffUL, 0x80000000UL, 0xffffffffUL,
        0x100000000UL, 0x7fffffffffffffffUL,
        0x8000000000000000UL, 0xfffffffffffffffeUL,
        0xffffffffffffffffUL
    };
    static const int narrow_values[] = {
        INT_MIN, -65537, -1, 0, 1, 65537, INT_MAX
    };
    unsigned long seed = 0x9e3779b97f4a7c15UL;
    unsigned int i;
    for (i = 0U; i < 10000U; ++i) {
        unsigned long large;
        int narrow;
        int gnu_forward = 0, mini_forward = 0;
        int gnu_reverse = 0, mini_reverse = 0;
        int gf, mf, gr, mr;
        seed = seed * 6364136223846793005UL + 1442695040888963407UL;
        large = i < 70U ? large_values[(i / 7U) % 10U] : seed;
        narrow = i < 70U ? narrow_values[i % 7U] : (int)(seed >> 32);
        gf = gnu_mixed_forward(large, narrow, &gnu_forward);
        mf = minic_mixed_forward(large, narrow, &mini_forward);
        gr = gnu_mixed_reverse(narrow, large, &gnu_reverse);
        mr = minic_mixed_reverse(narrow, large, &mini_reverse);
        if (gf != mf || gr != mr || gnu_forward != mini_forward ||
            gnu_reverse != mini_reverse || gf != gr ||
            gnu_forward != gnu_reverse)
            return 1;
    }
    return 42;
}
