/* Native 128-bit integer Core arithmetic, shifts, comparisons, casts and
 * by-value ABI. RV32I must preserve the same unsigned 128-bit semantics. */
typedef unsigned __int128 MirU128;
typedef __int128 MirI128;

volatile unsigned long mir_wide_sink;

__attribute__((noinline))
MirU128 mir_wide_mul(MirU128 a, MirU128 b, unsigned shift) {
    MirU128 mixed = a * b + (a ^ b);
    mixed ^= (a << (shift & 63U));
    return (mixed >> 17U) + (mixed << 9U);
}

__attribute__((noinline))
long mir_wide_compare(MirI128 a, MirI128 b) {
    MirI128 q = a * 13 - b;
    MirI128 r = (a >> 19U) ^ (b << 7U);
    MirU128 x = mir_wide_mul((MirU128)q, (MirU128)r, 13U);
    return (long)(x > (MirU128)a ? x : (MirU128)(a + r));
}

int main(void) {
    MirI128 left = ((MirI128)0x12345678 << 67U) + 0x99887766;
    MirI128 right = ((MirI128)0x44556677 << 31U) + 23;
    long value = mir_wide_compare(left, right);
    mir_wide_sink = (unsigned long)value;
    return (int)(value & 255L);
}
