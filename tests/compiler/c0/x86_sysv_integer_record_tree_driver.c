/* GCC oracle caller for x86_sysv_integer_record_tree.c. GNU's ABI
 * classification is independent of the MiniC-compiled callee. */
typedef struct { unsigned int lo; unsigned int hi; } OneGP;
typedef union { unsigned long all; OneGP parts; } TwoHalf;
typedef struct { TwoHalf first; unsigned long second; } TwoGP;
typedef struct { unsigned long items[2]; } ArrayTwoGP;
typedef struct __attribute__((packed)) { unsigned int lo:3, hi:29; } PackedBitGP;

extern OneGP x86_one_roundtrip(OneGP);
extern TwoGP x86_two_roundtrip(TwoGP);
extern ArrayTwoGP x86_array_roundtrip(ArrayTwoGP);
extern PackedBitGP x86_bit_roundtrip(PackedBitGP);

int main(void) {
    OneGP a = {13U, 0xaabbccddU};
    TwoGP b;
    ArrayTwoGP c = {{19UL, 0x11223344UL}};
    PackedBitGP d = {5U, 0x4321U};
    b.first.parts.lo = 0xabcdef89U;
    b.first.parts.hi = 0x01234567U;
    b.second = 0x99887766UL;
    a = x86_one_roundtrip(a);
    b = x86_two_roundtrip(b);
    c = x86_array_roundtrip(c);
    d = x86_bit_roundtrip(d);
    if (a.lo != 16U || a.hi != (0xaabbccddU ^ 0x5a5aU)) return 1;
    if (b.first.parts.lo != (0xabcdef89U ^ 0x1234U) ||
        b.first.parts.hi != 0x01234567U ||
        b.second != 0x99887867UL) return 2;
    if (c.items[0] != 36UL || c.items[1] != (0x11223344UL ^ 0x12345678UL)) return 3;
    if (d.lo != (5U ^ 3U) || d.hi != 0x4338U) return 4;
    return 0;
}
