/* MC-076/P49: GCC and MiniC must see exactly this same source.  Direct
 * stores in .head.text use relocated physical memory; a helper's numeric
 * pointer result instead denotes the original ELF link VMA. */
volatile unsigned long p49_head_global
    __attribute__((section(".head.data"))) = 0x2211UL;

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_helper(volatile unsigned long *pointer) {
    return (unsigned long)pointer;
}

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_entry(unsigned long value) {
    p49_head_global = value;
    return p49_head_helper(&p49_head_global);
}
