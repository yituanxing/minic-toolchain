/* Linux 6.6.143 patch_map shape: guarded fix_to_virt behind an ordinary
 * static wrapper with arm64's real BUG_ON metadata/BRK inline asm. Keep this
 * separate from the no-ASM green regression until Core can clone it safely. */
enum fixed_addresses { FIX_HOLE, FIX_TEXT_POKE0, FIX_TEXT_POKE1,
                       __end_of_fixed_addresses };
extern void original_set_fixmap(unsigned int, unsigned long, unsigned long);

/* CONFIG_GENERIC_BUG + CONFIG_DEBUG_BUGVERBOSE: GAS-local numeric labels
 * cross section push/pop and create ELF __bug_table relative fixups. */
#define ORIGINAL_STRINGIFY_1(value) #value
#define ORIGINAL_STRINGIFY(value) ORIGINAL_STRINGIFY_1(value)
#define ORIGINAL_ASM_BUG_FLAGS(flags) \
    ".pushsection __bug_table,\"aw\";\n" \
    ".align 2;\n" \
    "14470: .long 14471f - .;\n" \
    ".pushsection .rodata.str,\"aMS\",@progbits,1;\n" \
    "14472: .string \"" __FILE__ "\";\n" \
    ".popsection;\n" \
    ".long 14472b - .;\n" \
    ".short " ORIGINAL_STRINGIFY(__LINE__) ";\n" \
    ".short " #flags ";\n" \
    ".align 2;\n" \
    ".popsection;\n" \
    "14471: brk 0x800\n"
#define ORIGINAL_BUG() do { asm volatile(ORIGINAL_ASM_BUG_FLAGS(0)); \
    __builtin_unreachable(); } while (0)
#define ORIGINAL_BUG_ON(condition) do { if (__builtin_expect(!!(condition), 0)) \
    ORIGINAL_BUG(); } while (0)

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
    ORIGINAL_BUG_ON(!addr);
    return (void *)set_fixmap_offset(fixmap, (unsigned long)addr);
}
void *patch_entry(void *addr) { return patch_map(addr, FIX_TEXT_POKE0); }
/* Two distinct constant callsites require separate clones. Their repeated
 * numeric labels must remain local to each emitted __bug_table entry. */
void *patch_entry_two(void *addr) { return patch_map(addr, FIX_TEXT_POKE1); }
