/* MC-076/P51: a pointer from the early-head physical global crosses two
 * direct-call ABI boundaries.  The first callee forwards it to a second
 * helper and returns that CALL result, rather than its own parameter SSA.
 * A one-level parameter-to-RETURN census misses this alias route. */
volatile unsigned long p49_head_global
    __attribute__((section(".head.data"))) = 0x2211UL;

__attribute__((section(".head.text"), noinline, used))
volatile unsigned long *p49_head_helper(volatile unsigned long *pointer) {
    return pointer;
}

__attribute__((section(".head.text"), noinline, used))
volatile unsigned long *p51_head_wrapper(volatile unsigned long *pointer) {
    return p49_head_helper(pointer);
}

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_entry(unsigned long value) {
    p49_head_global = value;
    return (unsigned long)p51_head_wrapper(&p49_head_global);
}
