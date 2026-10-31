/* Linux 6.6 i915_reg_defs.h uses BUILD_BUG_ON_ZERO(-!!(condition)) and
 * __is_constexpr to validate register bitfields at compile time. The C
 * conditional operator must distinguish an integer ICE zero cast to void*
 * from an otherwise identical runtime expression: only the ICE is a null
 * pointer constant, so the conditional's pointer type changes. */
#define KERNEL_IS_CONSTEXPR(x) \
    (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0L)) : (int *)8)))
#define KERNEL_BUILD_BUG_ON_ZERO(e) ((int)sizeof(struct { int : (-!!(e)); }))
#define KERNEL_BF_SHF(x) (__builtin_ffsll(x) - 1)
#define KERNEL_POWER_OF_TWO(x) ((x) && (((x) & ((x) - 1)) == 0))
#define KERNEL_PREP(mask, value) \
    ((unsigned)((((__typeof__(mask))(value) << KERNEL_BF_SHF(mask)) & (mask)) + \
    KERNEL_BUILD_BUG_ON_ZERO(!KERNEL_IS_CONSTEXPR(mask)) + \
    KERNEL_BUILD_BUG_ON_ZERO((mask) == 0 || (mask) > 4294967295U) + \
    KERNEL_BUILD_BUG_ON_ZERO(!KERNEL_POWER_OF_TWO((mask) + \
                               (1ULL << KERNEL_BF_SHF(mask)))) + \
    KERNEL_BUILD_BUG_ON_ZERO(__builtin_choose_expr( \
        KERNEL_IS_CONSTEXPR(value), \
        (~((mask) >> KERNEL_BF_SHF(mask)) & (value)), 0))))

#if defined(REJECT_ZERO_MASK)
unsigned reject_bad_mask(void) { return KERNEL_PREP(0U, 2U); }
#elif defined(REJECT_OUT_OF_RANGE_VALUE)
unsigned reject_bad_value(void) { return KERNEL_PREP(0xF0U, 32U); }
#else
_Static_assert(KERNEL_IS_CONSTEXPR(0xF0U), "integer ICE zero is a null pointer constant");
_Static_assert(sizeof(*(8 ? (void *)((long)0xF0U * 0L) : (int *)8)) == sizeof(int),
               "ICE zero must retain the int pointer arm");

unsigned kernel_i915_bitfield_probe(unsigned runtime) {
    /* Runtime x * 0 is still NOT an integer constant expression. The
     * conditional must instead select composite void*, GNU sizeof(void)=1. */
    _Static_assert(!KERNEL_IS_CONSTEXPR(runtime), "runtime zero is not an ICE");
    _Static_assert(sizeof(*(8 ? (void *)((long)runtime * 0L) : (int *)8)) == 1,
                   "non-ICE conditional must retain void pointer arm");
    return KERNEL_PREP(0xF0U, 2U) + KERNEL_PREP(0xF0U, runtime);
}
#endif
