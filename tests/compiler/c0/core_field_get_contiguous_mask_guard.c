/* Linux KVM-style FIELD_GET with its compile-time contiguous-mask guard.
 * Valid masks may be proven as pure no-op guards; malformed masks must retain
 * the real diagnostic call. This is not an instruction for the compiler to
 * ignore __attribute__((error)) or erase an arbitrary function call. */
#define FIELD_SHIFT(mask) (__builtin_ffsll(mask) - 1)
#define FIELD_GET_GUARDED(mask, reg) ({ \
    do { \
        extern void impossible_noncontiguous_bitfield_mask(void) \
            __attribute__((error("FIELD_GET requires a contiguous mask"))); \
        if ((((mask) + ((mask) & (-(mask)))) & \
             (((mask) + ((mask) & (-(mask)))) - 1ULL)) != 0ULL) \
            impossible_noncontiguous_bitfield_mask(); \
    } while (0); \
    (((reg) & (mask)) >> FIELD_SHIFT(mask)); \
})

unsigned long long valid_field_get(void) {
    return FIELD_GET_GUARDED(0x70ULL, 0x50ULL);
}

#ifdef MINIC_BAD_MASK_NEGATIVE
unsigned long long invalid_field_get(void) {
    /* 0xa0 = 1010_0000b has a hole: compile-time diagnostic must survive. */
    return FIELD_GET_GUARDED(0xa0ULL, 0xa0ULL);
}
#else
int main(void) {
    return valid_field_get() == 5ULL ? 0 : 1;
}
#endif
