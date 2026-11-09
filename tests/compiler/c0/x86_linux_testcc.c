#include <stdbool.h>

/* Linux constant_test_bit_acquire emits the same ZF flag-output pattern. */
static bool test_nz(unsigned char *byte) {
    bool result;
    asm volatile("testb %2,%1\n\t/* output condition code nz*/\n"
        : "=@ccnz" (result)
        : "m" (*byte), "i" (8)
        : "memory");
    return result;
}

static bool test_z(unsigned char *byte) {
    bool result;
    asm volatile("testb %2,%1\n\t/* output condition code z*/\n"
        : "=@ccz" (result)
        : "m" (*byte), "i" (16)
        : "memory");
    return result;
}

static bool test_non_power_mask(unsigned char *byte) {
    bool result;
    asm volatile("testb %2,%1\n\t/* output condition code nz*/\n"
        : "=@ccnz" (result)
        : "m" (*byte), "i" (3)
        : "memory");
    return result;
}

int main(void) {
    unsigned char zero = 0, selected = 8, other = 16, mixed = 2;
    return test_nz(&zero) || !test_nz(&selected) || test_nz(&other) ||
        !test_z(&zero) || !test_z(&selected) || test_z(&other) ||
        test_non_power_mask(&zero) || !test_non_power_mask(&mixed);
}
