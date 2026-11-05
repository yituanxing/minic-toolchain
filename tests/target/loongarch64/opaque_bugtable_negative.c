/* Each -D case breaks one sealed Linux __bug_table grammar property.
 * The source is intentionally not a valid qualified opaque ASM operation. */
void loong_opaque_bugtable_bad(void) {
#if defined(MINIC_BAD_BUG_PATH)
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c; break 0\"; .popsection; "
        ".long 10002b - .; .short 413; .short 0; .popsection; "
        "10001: break 1");
#elif defined(MINIC_BAD_BUG_LABEL)
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10003: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; "
        ".long 10002b - .; .short 413; .short 0; .popsection; "
        "10001: break 1");
#else
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; "
        ".long 10002b - .; .short 413; .short 0; .popsection; "
        "10001: break 1; dbar 1");
#endif
}
