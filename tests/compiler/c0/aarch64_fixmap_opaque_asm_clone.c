/* Linux 6.6.143 arch/arm64/kernel/patching.c: a TU-local non-inline
 * wrapper holds a tied-register empty GNU asm, an empty volatile asm, and
 * a closed-numeric-label BUG table. The caller's constant fixmap index
 * must pass through the wrapper to discharge the inline BUILD_BUG_ON.
 * Cloning any arbitrary ASM with external labels is intentionally unsafe. */
enum fixed_addresses { FIX_HOLE, FIX_TEXT_POKE0, __end_of_fixed_addresses };
extern void minic_bad_fixmap(void)
    __attribute__((error("fixmap index must be constant and in range")));

static __attribute__((always_inline)) inline unsigned long
fix_to_virt(unsigned int idx)
{
    if (idx >= __end_of_fixed_addresses)
        minic_bad_fixmap();
    return 0xffff000000000000UL - ((unsigned long)idx << 12);
}

static unsigned long patch_map(unsigned long addr, int fixmap)
{
    unsigned long hidden;
    __asm__("" : "=r"(hidden) : "0"(addr));
    if (!addr) {
        __asm__ volatile(
            ".pushsection __bug_table,\"aw\"; .align 2; "
            "14470: .long 14471f - .; "
            ".pushsection .rodata.str,\"aMS\",@progbits,1; "
            "14472: .string \"aarch64-patching-fixture\"; "
            ".popsection; .long 14472b - .; .short 44; "
            ".short 0; .align 2; .popsection; 14471: brk 0x800");
        __asm__ volatile("");
        __builtin_unreachable();
    }
    return hidden + fix_to_virt(fixmap);
}

unsigned long minic_aarch64_fixmap_clone_entry(unsigned long addr)
{
    return patch_map(addr, FIX_TEXT_POKE0);
}
