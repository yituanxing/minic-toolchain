extern unsigned long core_m23_choose(int, unsigned long, unsigned long);
extern unsigned long core_m23_swab_shape(unsigned long);

int main(void) {
    if (core_m23_choose(0, 11UL, 22UL) != 22UL)
        return 1;
    if (core_m23_choose(1, 11UL, 22UL) != 11UL)
        return 2;
    if (core_m23_swab_shape(0x0123456789abcdefUL) != 0xefcdab8967452301UL)
        return 3;
    return 0;
}
