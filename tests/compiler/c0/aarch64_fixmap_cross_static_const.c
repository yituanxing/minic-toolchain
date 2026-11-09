/* Linux 6.6.143: patch_map(fixmap) -> set_fixmap_offset -> fix_to_virt.
 * A plain TU-local static wrapper must not prevent __always_inline's
 * compile-time assertion from seeing its constant enum caller. */
enum fixed_addresses { FIX_HOLE, FIX_TEXT_POKE0, __end_of_fixed_addresses };
extern void original_set_fixmap(unsigned int, unsigned long, unsigned long);
static __attribute__((always_inline)) inline unsigned long fix_to_virt(unsigned int idx) {
    do { extern void __compiletime_assert_fixmap(void)
        __attribute__((error("fixmap index must be constant in range")));
        if (!(!(idx >= __end_of_fixed_addresses))) __compiletime_assert_fixmap();
    } while (0);
    return 0xffff000000000000UL - ((unsigned long)idx << 12);
}
#define set_fixmap_offset(idx, phys) ({ unsigned long address; \
    original_set_fixmap((idx), (phys), 0); \
    address = fix_to_virt((idx)) + ((phys) & 4095UL); address; })
static void *patch_map(void *addr, int fixmap) {
    return (void *)set_fixmap_offset(fixmap, (unsigned long)addr);
}
void *patch_entry(void *addr) { return patch_map(addr, FIX_TEXT_POKE0); }
