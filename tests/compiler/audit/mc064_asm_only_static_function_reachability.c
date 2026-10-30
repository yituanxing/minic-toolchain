/* MC-064: static function used only as GNU ASM symbolic immediate must not
 * be pruned. -DMC064_DIRECT_CALL supplies the C direct-call positive control. */
static int asm_only_target(int x) { return x + 19; }
int anchor(void) {
    __asm__ volatile(
        ".pushsection .rodata\n\t"
        ".quad %c0\n\t"
        ".popsection"
        : : "i"(asm_only_target));
#ifdef MC064_DIRECT_CALL
    return asm_only_target(4);
#else
    return 23;
#endif
}
