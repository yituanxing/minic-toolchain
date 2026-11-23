/* MC-076/P53: this early-head callee has no definition in the current
 * source module.  The compiler must not assume an external helper never
 * forwards a physically addressed global into a numeric link-VMA use. */
volatile unsigned long p49_head_global
    __attribute__((section(".head.data"))) = 0x2211UL;

extern volatile unsigned long *p53_external_identity(
    volatile unsigned long *pointer);

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_entry(unsigned long value) {
    p49_head_global = value;
    return (unsigned long)p53_external_identity(&p49_head_global);
}
