/* GNU LoongArch64 side; GNU caller checks MiniC indirect-call results,
 * while GNU callees interpret the hidden-result and stack argument ABI. */
#include <stdio.h>

struct L17 { unsigned char b[17]; };
struct L24 { unsigned long w[3]; };
struct L40 { unsigned long w[5]; };

extern struct L17 minic_indirect_17(unsigned char);
extern struct L24 minic_indirect_24(unsigned long);
extern struct L40 minic_indirect_40(unsigned long);

static struct L17 gnu17(unsigned char v) {
    struct L17 result;
    unsigned i;
    for (i = 0; i < 17; ++i) result.b[i] = (unsigned char)(v + i);
    return result;
}
static struct L24 gnu24(unsigned long v) {
    struct L24 result = {{ v, v + 11UL, v + 22UL }};
    return result;
}
static struct L40 gnu40(unsigned long a, unsigned long b, unsigned long c,
                        unsigned long d, unsigned long e, unsigned long f,
                        unsigned long g, unsigned long h, unsigned long i,
                        unsigned long j) {
    struct L40 result = {{ a + b, c + d, e + f, g + h, i + j }};
    return result;
}

struct L17 (*make17)(unsigned char) = gnu17;
struct L24 (*make24)(unsigned long) = gnu24;
struct L40 (*make40)(unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long,
                     unsigned long) = gnu40;

int main(void) {
    struct L17 a = minic_indirect_17(7);
    struct L24 b = minic_indirect_24(31UL);
    struct L40 c = minic_indirect_40(21UL);
    unsigned i;
    for (i = 0U; i < 17U; ++i)
        if (a.b[i] != (unsigned char)(i + 7U)) return 1;
    if (b.w[0] != 31UL || b.w[1] != 42UL || b.w[2] != 58UL)
        return 2;
    if (c.w[0] != 3UL || c.w[1] != 7UL || c.w[2] != 11UL ||
        c.w[3] != 15UL || c.w[4] != 30UL)
        return 3;
    puts("LOONGARCH64_INDIRECT_SRET_OK");
    return 42;
}
