/* MC-059: independent consumer TU. Exit 42 means all ABI checks passed. */
union U64 { double d; unsigned long long bits; };
union OnlyFP { double d; };
struct Nested { unsigned long long tag; union U64 data; };
struct Array { union U64 items[2]; };

extern union U64 abi_echo(union U64);
extern unsigned long long abi_bits(union U64);
extern struct Nested abi_nested(struct Nested);
extern struct Array abi_array(struct Array);
extern union OnlyFP abi_only_fp(union OnlyFP);
extern double abi_scalar(double);

int main(void) {
    union U64 u;
    struct Nested n;
    struct Array a;
    union OnlyFP fp;
    u.bits = 0x123456789abcdef0ULL;
    if (abi_echo(u).bits != u.bits) return 1;
    if (abi_bits(u) != u.bits) return 2;
    n.tag = 0x1020304050607080ULL;
    n.data.bits = 0xfedcba9876543210ULL;
    n = abi_nested(n);
    if (n.tag != 0x1020304050607080ULL ||
        n.data.bits != 0xfedcba9876543210ULL) return 3;
    a.items[0].bits = 0x1122334455667788ULL;
    a.items[1].bits = 0x8877665544332211ULL;
    a = abi_array(a);
    if (a.items[0].bits != 0x1122334455667788ULL ||
        a.items[1].bits != 0x8877665544332211ULL) return 4;
    fp.d = 6.5;
    if (abi_only_fp(fp).d != 6.5) return 5;
    if (abi_scalar(1.25) != 3.25) return 6;
    return 42;
}
