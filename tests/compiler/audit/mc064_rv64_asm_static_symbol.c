/* MC-064: GNU RV64-valid `%0` symbolic immediate fixture.
 *
 * Plain build: function reachable *only* by the .rodata relocation in ASM.
 * -DMC064_DIRECT_CALL: ordinary Core CALL keeps the function alive.
 * -DMC064_STATIC_OBJECT: control for an ASM-only static object relocation.
 *
 * Do not replace `%0` with the x86-specific `%c0` GNU operand modifier. */
#ifdef MC064_STATIC_OBJECT
static int asm_only_object = 37;
#define MC064_SYMBOL (&asm_only_object)
#else
static int asm_only_target(int x) { return x + 19; }
#define MC064_SYMBOL asm_only_target
#endif

int anchor(void) {
    __asm__ volatile(".pushsection .rodata\n\t"
                     ".dword %0\n\t"
                     ".popsection"
                     : : "i"(MC064_SYMBOL));
#ifdef MC064_DIRECT_CALL
    return asm_only_target(4);
#else
    return 11;
#endif
}
