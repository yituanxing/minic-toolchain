/* Native GCC differential for the typed x86 SysV pair/record ABI.
 * Exercise odd INTEGER eightbytes, >128-byte stack copies, mixed register
 * exhaustion, i128 stack fallback, and both signed/unsigned limb shifts. */
typedef unsigned __int128 U128;
typedef __int128 I128;

struct Odd3 { unsigned short a; unsigned char b; } __attribute__((packed));
struct Odd11 { unsigned long a; unsigned short b; unsigned char c; }
    __attribute__((packed));
struct Odd15 {
    unsigned long a;
    unsigned int b;
    unsigned short c;
    unsigned char d;
} __attribute__((packed));
struct Large { unsigned char bytes[233]; unsigned long tail; };

volatile unsigned long abi_pair_sink;

__attribute__((noinline))
struct Odd3 odd3_make(unsigned long a) {
    struct Odd3 result;
    result.a = (unsigned short)(a ^ 0xabcU);
    result.b = (unsigned char)(a >> 4U);
    return result;
}

__attribute__((noinline))
struct Odd11 odd11_make(unsigned long a, unsigned long b) {
    struct Odd11 result;
    result.a = a + 17U;
    result.b = (unsigned short)(b ^ 0x1234U);
    result.c = (unsigned char)(a >> 19U);
    return result;
}

__attribute__((noinline))
struct Odd15 odd15_make(unsigned long a, unsigned long b) {
    struct Odd15 result;
    result.a = a ^ 0x123456789abcdef0UL;
    result.b = (unsigned int)b;
    result.c = (unsigned short)(a >> 14U);
    result.d = (unsigned char)(b >> 8U);
    return result;
}

__attribute__((noinline))
struct Large large_make(unsigned long seed) {
    struct Large result;
    unsigned int i;
    for (i = 0U; i < sizeof(result.bytes); ++i)
        result.bytes[i] = (unsigned char)(seed + i * 13U);
    result.tail = seed ^ 0xaabbccddeeff0011UL;
    return result;
}

__attribute__((noinline))
U128 pair_mix(unsigned long p0, U128 a, unsigned long p1,
    unsigned long p2, unsigned long p3, U128 b, unsigned long p4) {
    U128 product = a * b;
    product ^= (a >> 65U);
    product += (b << 63U);
    product -= (a & b);
    product |= a ^ b;
    return product + (U128)(p0 + p1 + p2 + p3 + p4);
}

__attribute__((noinline))
unsigned long sum_records(struct Odd3 small, struct Odd11 pair,
    struct Odd15 last, struct Large large, unsigned long a,
    unsigned long b) {
    unsigned long sum = small.a + small.b + pair.a + pair.b + pair.c;
    unsigned int i;
    sum += last.a + last.b + last.c + last.d + a + b + large.tail;
    for (i = 0U; i < sizeof(large.bytes); ++i)
        sum += (unsigned long)large.bytes[i] * (i + 1U);
    return sum;
}

int main(void) {
    struct Odd3 small = odd3_make(0x1234abcdUL);
    struct Odd11 pair = odd11_make(0x12345678UL, 0x99887766UL);
    struct Odd15 last = odd15_make(0xaabbccddUL, 0x44556677UL);
    struct Large large = large_make(0x123456789abcdef0UL);
    U128 a = ((U128)0x123456789abcdef0UL << 64U) | 0x0f1e2d3c4b5a6978UL;
    U128 b = ((U128)0xfedcba9876543210UL << 64U) | 0x8877665544332211UL;
    U128 result = pair_mix(7UL, a, 11UL, 13UL, 17UL, b, 19UL);
    unsigned long checksum = sum_records(small, pair, last, large, 31UL, 37UL);
    unsigned int i;
    for (i = 0U; i < 127U; ++i) {
        I128 negative = -((I128)(a ^ (b >> 17U)));
        U128 r = (a << i) ^ (b >> i) ^ (U128)(negative >> i);
        checksum ^= (unsigned long)(r + (r >> 64U));
        a = a * (U128)0x9e3779b97f4a7c15UL + b + (U128)i;
        b = (b ^ (a >> 11U)) * (U128)0xd6e8feb86659fd93UL;
    }
    checksum ^= (unsigned long)result ^ (unsigned long)(result >> 64U);
    abi_pair_sink = checksum;
    return (int)(checksum & 255UL);
}
