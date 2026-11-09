/* Linux 6.6.143 fs/proc/task_mmu.c -> include/linux/swapops.h:
 * is_pfn_swap_entry(): BUILD_BUG_ON(SWP_TYPE_SHIFT < SWP_PFN_BITS).
 * The exact frozen GNU .i spells the bound as
 *     58 < ((feature ? 52 : 46) - 12).
 * This standalone model is NOT the frozen original and must not certify
 * whole-kernel GNU LD/QEMU on its own. */
typedef unsigned long u64;
static volatile unsigned int observed_feature_reads;

extern void __compiletime_assert_347(void);

static unsigned int boot_cpu_feature(void)
{
    ++observed_feature_reads;
    return observed_feature_reads & 1U;
}

static u64 is_pfn_swap_entry_guard_model(void)
{
    u64 pfn_bits = (boot_cpu_feature() ? 52UL : 46UL) - 12UL;
    if (58UL < pfn_bits)
        __compiletime_assert_347();
    return pfn_bits;
}

int main(void)
{
    u64 first = is_pfn_swap_entry_guard_model();
    u64 second = is_pfn_swap_entry_guard_model();
    return first == 40UL && second == 34UL &&
           observed_feature_reads == 2U ? 42 : 1;
}

#ifdef MINIC_NEGATIVE_CASES
extern void runtime_pfn_bits_probe(void);
extern u64 dangerous_pfn_bits_source(void);

/* A runtime-dependent branch may genuinely produce a value > 58. */
u64 dynamic_high_pfn_bits(int active)
{
    u64 bits = (active ? dangerous_pfn_bits_source() : 46UL) - 12UL;
    if (58UL < bits)
        runtime_pfn_bits_probe();
    return bits;
}

/* A literal high branch is still unsafe, despite the other being 46. */
u64 fixed_high_pfn_bits(int active)
{
    u64 bits = (active ? 80UL : 46UL) - 12UL;
    if (58UL < bits)
        runtime_pfn_bits_probe();
    return bits;
}

/* A changed local must not inherit the original two-constant bound. */
u64 escaped_pfn_bits(int active)
{
    u64 bits = (active ? 52UL : 46UL) - 12UL;
    bits |= dangerous_pfn_bits_source();
    if (58UL < bits)
        runtime_pfn_bits_probe();
    return bits;
}
#endif
