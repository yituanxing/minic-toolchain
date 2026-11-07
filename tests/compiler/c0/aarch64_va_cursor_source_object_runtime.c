/* AAPCS64 builtin va_list is a 32-byte mutable struct value, not void *.
 * Build with an AArch64 GNU toolchain/QEMU when available; on MiniC the
 * preexisting proxy must fail explicitly until real source layout lands. */
typedef __builtin_va_list MiniVa;

struct EmbeddedVa {
    unsigned char prefix;
    MiniVa cursor;
    unsigned char suffix;
};

static MiniVa global_cursor;
static MiniVa pair_of_cursors[2];

static unsigned long sum_after_copy(unsigned long fixed, ...) {
    MiniVa current;
    MiniVa copy;
    unsigned long first, second, third;
    __builtin_va_start(current, fixed);
    first = __builtin_va_arg(current, unsigned long);
    __builtin_va_copy(copy, current);
    second = __builtin_va_arg(copy, unsigned long);
    third = __builtin_va_arg(current, unsigned long);
    __builtin_va_end(copy);
    __builtin_va_end(current);
    return first * 100UL + second * 10UL + third;
}

int main(void) {
    MiniVa local;
    struct EmbeddedVa container;
    if (sizeof(MiniVa) != 32U || sizeof(local) != 32U ||
        sizeof(global_cursor) != 32U) return 10;
    if (sizeof(pair_of_cursors) != 64U || sizeof(container) != 48U ||
        sizeof(container.cursor) != 32U) return 11;
    if ((char *)(&local + 1) - (char *)&local != 32) return 12;
    return sum_after_copy(99UL, 1UL, 2UL) == 122UL ? 0 : 13;
}
