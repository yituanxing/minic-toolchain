/* Every checked-overflow result_address is a real C lvalue, including a
 * GNU packed field which can be misaligned relative to its integer width. */
struct __attribute__((packed)) OverflowPacked {
    unsigned char lead;
    signed short narrow;
    int word;
    unsigned long long wide;
};

int packed_overflow_narrow(struct OverflowPacked *p) {
    return __builtin_add_overflow((short)32767, (short)1, &p->narrow);
}
int packed_overflow_add(struct OverflowPacked *p) {
    return __builtin_add_overflow(2147483647, 1, &p->word);
}
int packed_overflow_sub(struct OverflowPacked *p) {
    return __builtin_sub_overflow(-2147483647 - 1, 1, &p->word);
}
int packed_overflow_mul(struct OverflowPacked *p) {
    return __builtin_mul_overflow(100000, 100000, &p->word);
}
int packed_overflow_wide(struct OverflowPacked *p) {
    return __builtin_mul_overflow(0x100000000ULL, 0x100000000ULL, &p->wide);
}

int main(void) {
    struct OverflowPacked p;
    p.lead = 27;
    if (!packed_overflow_narrow(&p) || p.narrow != (short)-32768)
        return 1;
    if (!packed_overflow_add(&p) || p.word != (-2147483647 - 1))
        return 2;
    if (!packed_overflow_sub(&p) || p.word != 2147483647)
        return 3;
    if (!packed_overflow_mul(&p) || p.word != 1410065408)
        return 4;
    if (!packed_overflow_wide(&p) || p.wide != 0ULL || p.lead != 27)
        return 5;
    return 0;
}
