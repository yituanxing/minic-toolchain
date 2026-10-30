/* Fixed Linux 6.6.143 arch/x86/mm/kaslr.c: kernel_randomize_memory().
 * Source-level model of the original GNU frozen .i:30937-30941.  The
 * pgtable_l5_enabled() condition is runtime-dependent and MUST run, but
 * both candidate address-space bases lie below CPU_ENTRY_AREA_BASE.
 * Not an authenticated execution of the complete frozen Kbuild owner. */
#define __PAGE_OFFSET_BASE_L5 0xff11000000000000UL
#define __PAGE_OFFSET_BASE_L4 0xffff888000000000UL
#define CPU_ENTRY_AREA_BASE ((-4UL) << 39)

static const unsigned long vaddr_end = CPU_ENTRY_AREA_BASE;
extern void __compiletime_assert_287(void);

static int probe_count;
static int pgtable_l5_enabled(int config)
{
    probe_count++;
    return config != 0;
}

static unsigned long kernel_randomize_memory_shape(int config)
{
    unsigned long vaddr_start;
    unsigned long vaddr;
    vaddr_start = pgtable_l5_enabled(config) ?
        __PAGE_OFFSET_BASE_L5 : __PAGE_OFFSET_BASE_L4;
    vaddr = vaddr_start;
    if (vaddr_start >= vaddr_end)
        __compiletime_assert_287();
    return vaddr;
}

int main(void)
{
    unsigned long l5 = kernel_randomize_memory_shape(1);
    unsigned long l4 = kernel_randomize_memory_shape(0);
    return probe_count == 2 && l5 == __PAGE_OFFSET_BASE_L5 &&
           l4 == __PAGE_OFFSET_BASE_L4 ? 42 : 1;
}

#ifdef MINIC_NEGATIVE_CASES
extern void observed_above_end(void);
static volatile unsigned long dynamic_address;

unsigned long kaslr_dynamic_invalid(int config)
{
    unsigned long start = config ? __PAGE_OFFSET_BASE_L5 : dynamic_address;
    if (start >= vaddr_end)
        observed_above_end();
    return start;
}

unsigned long kaslr_static_invalid(int config)
{
    unsigned long start = config ? __PAGE_OFFSET_BASE_L5 : ~0UL;
    if (start >= vaddr_end)
        observed_above_end();
    return start;
}
#endif
