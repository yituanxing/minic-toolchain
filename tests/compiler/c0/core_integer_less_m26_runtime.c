int core_m26_signed_less(long left, long right);
int core_m26_unsigned_less(unsigned long left, unsigned long right);
int core_m26_mixed_less(unsigned int left, unsigned long right);
void core_m26_array_loop(unsigned int *dst, const unsigned int *src, unsigned long len);

int main(void) {
    unsigned int src[4] = {11U, 22U, 33U, 44U};
    unsigned int dst[4] = {0U, 0U, 0U, 0U};

    if (!core_m26_signed_less(-7L, 3L) || core_m26_signed_less(9L, -2L)) {
        return 1;
    }
    if (!core_m26_unsigned_less(3UL, 9UL) || core_m26_unsigned_less(~0UL, 1UL)) {
        return 2;
    }
    if (!core_m26_mixed_less(7U, 0x100000000UL) || core_m26_mixed_less(9U, 2UL)) {
        return 3;
    }
    core_m26_array_loop(dst, src, 4UL);
    if (dst[0] != 11U || dst[1] != 22U || dst[2] != 33U || dst[3] != 44U) {
        return 4;
    }
    return 0;
}
