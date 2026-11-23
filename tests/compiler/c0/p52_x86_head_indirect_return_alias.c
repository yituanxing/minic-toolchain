/* MC-076/P52: the .head.text pointer-returning function is reached through
 * a volatile function-pointer slot.  Physical STORE and numeric link VMA
 * need different relocations even when the target of the INDIRECT_CALL is
 * not resolved by the Core direct-call census. */
volatile unsigned long p49_head_global
    __attribute__((section(".head.data"))) = 0x2211UL;

typedef volatile unsigned long *(*P52Fn)(volatile unsigned long *);

__attribute__((section(".head.text"), noinline, used))
volatile unsigned long *p49_head_helper(volatile unsigned long *pointer) {
    return pointer;
}

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_entry(unsigned long value) {
    P52Fn volatile callback = p49_head_helper;
    p49_head_global = value;
    return (unsigned long)callback(&p49_head_global);
}
