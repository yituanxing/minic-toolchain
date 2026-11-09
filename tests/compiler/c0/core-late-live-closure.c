/* Linux-shaped Core reachability: an inline-ASM mnemonic shares a spelling
 * with an otherwise unreferenced static-inline function (cf. Linux wrmsr).
 * Core ASM symbol analysis conservatively exposes nop; lowering that body
 * exposes late_tail in a second reachability round. GCC does not conservatively
 * promote source functions named by ISA mnemonics, so its compile/link oracle
 * uses a `used` hint while the MiniC case exercises late discovery. */
#ifdef GNU_KEEP_ASM_REFERENCED_STATIC
#define GNU_KEEP __attribute__((used))
#else
#define GNU_KEEP
#endif

static int __attribute__((noinline)) late_tail(void) {
    return 39;
}

static inline GNU_KEEP int nop(void) {
    return late_tail() + 3;
}

extern int unresolved_dead_function(void);
static inline int unrelated_dead(void) {
    return unresolved_dead_function();
}

int main(void) {
    /* As in Linux's x86 wrmsr helper, the source graph does not reference
     * the C function whose assembler spelling equals this ISA mnemonic. */
    __asm__ volatile("nop");
    return 0;
}
