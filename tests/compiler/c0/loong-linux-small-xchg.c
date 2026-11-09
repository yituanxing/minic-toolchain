/* Original Linux 6.6 arch/loongarch/include/asm/cmpxchg.h
 * __xchg_small: a single 32-bit LL/SC reservation atomically exchanges
 * the selected 8/16-bit field without disturbing the adjoining fields.
 * Keep the exact GNU template, constraints and ordering. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long uptr;

static volatile u32 state = 0xa1b2c3d4U;

static u32 linux_xchg_small(volatile void *ptr, u32 val, u32 size) {
    u32 old32, mask, temp, shift;
    volatile u32 *ptr32;
    mask = (1U << (size * 8U)) - 1U;
    val &= mask;
    shift = ((uptr)ptr & 3UL) * 8U;
    mask <<= shift;
    ptr32 = (volatile u32 *)((uptr)ptr & ~3UL);
    __asm__ __volatile__(
        "1:\tll.w\t\t%0, %3\t\t\n"
        "\tandn\t\t%1, %0, %z4\t\n"
        "\tor\t\t%1, %1, %z5\t\n"
        "\tsc.w\t\t%1, %2\t\t\n"
        "\tbeqz\t\t%1, 1b\t\t\n"
        : "=&r" (old32), "=&r" (temp), "=ZC" (*ptr32)
        : "ZC" (*ptr32), "Jr" (mask), "Jr" (val << shift)
        : "memory");
    return (old32 & mask) >> shift;
}

/* Every byte and both halfwords must preserve their neighbors. The apparent
 * four independent stores below are intentionally all LL/SC updates of the
 * *same* enclosing aligned word; direct subword stores would not exercise
 * the contract, and stale/mis-shifted masks would corrupt adjacent bytes. */
static int test_all_offsets(void) {
    volatile u8 *bytes = (volatile u8 *)&state;
    volatile u16 *halves = (volatile u16 *)&state;
    state = 0x10203040U;
    if (linux_xchg_small(bytes, 0xabU, 1U) != 0x40U) return 1;
    if (state != 0x102030abU) return 2;
    if (linux_xchg_small(bytes + 1, 0xcdU, 1U) != 0x30U) return 3;
    if (state != 0x1020cdabU) return 4;
    if (linux_xchg_small(bytes + 2, 0xefU, 1U) != 0x20U) return 5;
    if (state != 0x10efcdabU) return 6;
    if (linux_xchg_small(bytes + 3, 0x78U, 1U) != 0x10U) return 7;
    if (state != 0x78efcdabU) return 8;
    if (linux_xchg_small(halves, 0x9001U, 2U) != 0xcdabU) return 9;
    if (state != 0x78ef9001U) return 10;
    if (linux_xchg_small(halves + 1, 0x0081U, 2U) != 0x78efU) return 11;
    if (state != 0x00819001U) return 12;
    return 0;
}

/* The return must be zero-extended exactly like the original Linux u32
 * function, even when the old byte/halfword's highest bit is set. A C
 * caller may then narrow to signed 8/16-bit, which must sign-extend there.
 * Repeat the reservation to catch incorrect SC status/output handling. */
static int test_sign_and_repetition(void) {
    volatile u8 *bytes = (volatile u8 *)&state;
    volatile u16 *halves = (volatile u16 *)&state;
    u32 i;
    state = 0x80008080U;
    if (linux_xchg_small(bytes + 1, 0x7fU, 1U) != 0x80U) return 1;
    if (state != 0x80007f80U) return 2;
    if ((signed char)linux_xchg_small(bytes, 0x01U, 1U) != -128)
        return 3;
    if (state != 0x80007f01U) return 4;
    if ((signed short)linux_xchg_small(halves + 1, 0x8001U, 2U)
        != (signed short)0x8000) return 5;
    if (state != 0x80017f01U) return 6;
    for (i = 0U; i < 64U; ++i) {
        u32 prior = (state >> 24) & 0xffU;
        u32 next = (i * 13U + 0x81U) & 0xffU;
        if (linux_xchg_small(bytes + 3, next, 1U) != prior) return 7;
        if ((state & 0x00ffffffU) != 0x00017f01U) return 8;
        if ((state >> 24) != next) return 9;
    }
    return 0;
}

int main(void) {
    volatile u8 *bytes = (volatile u8 *)&state;
    volatile u16 *halves = (volatile u16 *)&state;
    if (linux_xchg_small(bytes + 1, 0x71U, 1U) != 0xc3U) return 1;
    if (state != 0xa1b271d4U) return 2;
    if (linux_xchg_small(bytes + 3, 0x19aU, 1U) != 0xa1U) return 3;
    if (state != 0x9ab271d4U) return 4;
    if (linux_xchg_small(halves + 1, 0x5e47U, 2U) != 0x9ab2U) return 5;
    if (state != 0x5e4771d4U) return 6;
    if (linux_xchg_small(halves, 0x2d93U, 2U) != 0x71d4U) return 7;
    if (state != 0x5e472d93U) return 8;
    if (linux_xchg_small(bytes, 0x1bU, 1U) != 0x93U) return 9;
    if (state != 0x5e472d1bU) return 10;
    if (test_all_offsets()) return 11;
    if (test_sign_and_repetition()) return 12;
    return 42;
}
