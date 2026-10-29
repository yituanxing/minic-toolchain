/* Qualcomm Linux PLL tables: a static record field points at a row of an
 * external-style multi-dimensional array. The symbol relocation persists the
 * whole array owner and a byte addend, while its C type is pointer-to-element.
 */
struct Pll { int offset; const unsigned int *regs; int mode; };
static const unsigned int pll_regs[3][5] = {
    { 1, 2, 3, 4, 5 },
    { 6, 7, 8, 9, 10 },
    { 11, 12, 13, 14, 15 },
};
static const unsigned int nested_regs[2][3][4] = {
    { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } },
    { { 13, 14, 15, 16 }, { 17, 18, 19, 20 }, { 21, 22, 23, 24 } },
};
static struct Pll pll0 = { .offset = 8, .regs = pll_regs[0], .mode = 1 };
static struct Pll pll1 = { .offset = 9, .regs = pll_regs[2], .mode = 2 };
static struct Pll pll2 = { .offset = 10, .regs = nested_regs[1][2], .mode = 3 };
int main(void) {
    return pll0.regs[0] == 1 && pll1.regs[0] == 11 &&
           pll2.regs[0] == 21 && pll1.mode == 2 ? 0 : 1;
}
