/* MC-076/P49: a head helper returns the original pointer unchanged and
 * only its caller observes the numeric link VMA.  The callee contains no
 * pointer-to-integer cast, so a callee-only provenance check misses it. */
volatile unsigned long p49_head_global
    __attribute__((section(".head.data"))) = 0x2211UL;

__attribute__((section(".head.text"), noinline, used))
volatile unsigned long *p49_head_helper(volatile unsigned long *pointer) {
    return pointer;
}

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_entry(unsigned long value) {
    p49_head_global = value;
    return (unsigned long)p49_head_helper(&p49_head_global);
}
