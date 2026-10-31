/* One- and two-GPR SysV aggregate ABI classification must agree for nested
 * unions, arrays, integer bit-fields and ordinary aligned scalar members.
 * The driver is built independently by GCC; return and argument registers
 * therefore cannot be silently changed by recompiling both call ends. */
typedef struct { unsigned int lo; unsigned int hi; } OneGP;
typedef union { unsigned long all; OneGP parts; } TwoHalf;
typedef struct { TwoHalf first; unsigned long second; } TwoGP;
typedef struct { unsigned long items[2]; } ArrayTwoGP;
typedef struct __attribute__((packed)) { unsigned int lo:3, hi:29; } PackedBitGP;

__attribute__((noinline)) OneGP x86_one_roundtrip(OneGP value) {
    value.lo += 3U;
    value.hi ^= 0x5a5aU;
    return value;
}

__attribute__((noinline)) TwoGP x86_two_roundtrip(TwoGP value) {
    value.first.parts.lo ^= 0x1234U;
    value.second += 0x101U;
    return value;
}

__attribute__((noinline)) ArrayTwoGP x86_array_roundtrip(ArrayTwoGP value) {
    value.items[0] += 17UL;
    value.items[1] ^= 0x12345678UL;
    return value;
}

__attribute__((noinline)) PackedBitGP x86_bit_roundtrip(PackedBitGP value) {
    value.lo ^= 3U;
    value.hi += 23U;
    return value;
}
