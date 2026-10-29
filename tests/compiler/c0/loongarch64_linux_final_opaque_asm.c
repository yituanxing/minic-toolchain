/* Exact Linux 6.6.143 GNU native-defconfig LoongArch forms from
 * arch/loongarch/kernel/smp.c and arch/loongarch/kernel/relocate.c. */
void loong_linux_smp_bug_413(void) {
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; .long 10002b - .; "
        ".short 413; .short 0; .popsection; 10001: break 1");
}
void loong_linux_smp_bug_435(void) {
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; .long 10002b - .; "
        ".short 435; .short 0; .popsection; 10001: break 1");
}
void loong_linux_relocate_cache_sync(void) {
    __asm__ __volatile__("ibar 0 \t\n" "dbar 0 \t\n");
}
void loong_linux_idle_play_dead_asm(void) {
    __asm__ __volatile__("dbar 0\n\t" : : : "memory");
    __asm__ __volatile__("idle 0\n\t");
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; .long 10002b - .; "
        ".short 413; .short 0; .popsection; 10001: break 1");
}
void loong_linux_poll_play_dead_asm(void) {
    __asm__ __volatile__("dbar 0\n\t" : : : "memory");
    __asm__ __volatile__("nop\n\t");
    __asm__ __volatile__(
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; .long 10002b - .; "
        ".short 435; .short 0; .popsection; 10001: break 1");
}

#ifdef MINIC_LOONG_UNSUPPORTED_RAW_ASM
void loong_linux_unsupported_multiline_asm(void) {
    __asm__ __volatile__("ibar 0 \t\n" "dbar 1 \t\n");
}
#endif
