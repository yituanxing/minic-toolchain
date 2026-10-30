/* Fixed Linux v6.6.143 arch/x86/mm/ioremap.c and asm-generic/fixmap.h:
 * early_ioremap_init calls fix_to_virt(0), whose pure always-inline body
 * starts with the parameter-dependent BUILD_BUG_ON(idx >= limit). This
 * models the real frozen .i failure; it does not replace its SHA evidence. */
typedef unsigned int u32;
typedef unsigned long ulong;

extern void __compiletime_assert_183(void);
extern void __compiletime_assert_369(void);

static inline __attribute__((always_inline)) ulong fix_to_virt(u32 idx)
{
    do {
        if (idx >= 1024U)
            __compiletime_assert_183();
    } while (0);
    return 0xffffffffff5ff000UL - ((ulong)idx << 12);
}

int main(void)
{
    do {
        if ((fix_to_virt(0U) + 4096UL) & ((1UL << 21) - 1UL))
            __compiletime_assert_369();
    } while (0);
    return fix_to_virt(0U) == 0xffffffffff5ff000UL ? 42 : 1;
}

#ifdef MINIC_NEGATIVE_CASES
extern void runtime_guard_side_effect(u32);
volatile u32 observed_guard;

/* A nonconstant parameter may enter the diagnostic path. Do not treat the
 * pure guard-loop admission at call-site zero as a function-wide theorem. */
ulong negative_dynamic_idx(u32 idx)
{
    return fix_to_virt(idx);
}

/* A truly dynamic loop guard has observable side effects even when the
 * helper's returned integer is constant at the call site. */
static inline __attribute__((always_inline)) ulong volatile_guard(u32 idx)
{
    do {
        if (observed_guard)
            runtime_guard_side_effect(idx);
    } while (0);
    return 0xffffffffff5ff000UL - ((ulong)idx << 12);
}

ulong negative_volatile_guard(void)
{
    return volatile_guard(0U);
}
#endif
