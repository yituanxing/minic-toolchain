/* Same-source LoongArch pointer-offset stride differential: exact 2^n
 * scaling must preserve negative indices and unsigned wrap semantics. */
struct triple_byte { unsigned char bytes[3]; };
struct pair_long { long a, b; };
struct triple_long { long a, b, c; };

__attribute__((noinline)) long stride1(const char *p, long i) { return p[i]; }
__attribute__((noinline)) long stride2(const short *p, long i) { return p[i]; }
__attribute__((noinline)) long stride4(const int *p, long i) { return p[i]; }
__attribute__((noinline)) long stride8(const long *p, long i) { return p[i]; }
__attribute__((noinline)) long stride16(const struct pair_long *p, long i) { return p[i].b; }
__attribute__((noinline)) long stride3(const struct triple_byte *p, long i) { return p[i].bytes[0]; }
__attribute__((noinline)) long stride24(const struct triple_long *p, long i) { return p[i].c; }
__attribute__((noinline)) long stride_sub8(const long *p, long i) { return *(p - i); }
__attribute__((noinline)) long stride_neg8(const long *p, long i) { return p[-i]; }
__attribute__((noinline)) unsigned long stride_unsigned8(
    const unsigned long *p, unsigned long i) { return p[i]; }

int main(void) {
    char a[] = {11, 12, 13, 14};
    short b[] = {21, 22, 23, 24};
    int c[] = {31, 32, 33, 34};
    long d[] = {41, 42, 43, 44};
    unsigned long u[] = {81, 82, 83, 84};
    struct pair_long e[] = {{1, 51}, {2, 52}, {3, 53}, {4, 54}};
    struct triple_byte f[] = {{{61, 0, 0}}, {{62, 0, 0}}, {{63, 0, 0}}, {{64, 0, 0}}};
    struct triple_long g[] = {{1, 2, 71}, {3, 4, 72}, {5, 6, 73}, {7, 8, 74}};
    return stride1(&a[2], -1) != 12 || stride2(b, 2) != 23 ||
           stride4(c, 3) != 34 || stride8(d, 1) != 42 ||
           stride16(e, 2) != 53 || stride3(f, 1) != 62 ||
           stride24(g, 3) != 74 || stride_sub8(&d[3], 2) != 42 ||
           stride_neg8(&d[3], 1) != 43 || stride_unsigned8(u, 2UL) != 83UL;
}
