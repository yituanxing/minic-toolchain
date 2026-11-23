/* MC-076/P50: an early-head helper returns a physical pointer.  The
 * caller first stores it in a volatile local pointer slot and only then
 * converts the reloaded pointer to a link-VMA integer.  The GNU -O2 oracle
 * must keep a real stack store/load, so a direct CALL-result-bitcast-only
 * check is insufficient.  This is an intentional fail-closed case until
 * cross-function Core pointer provenance is modeled and emitted correctly. */
volatile unsigned long p49_head_global
    __attribute__((section(".head.data"))) = 0x2211UL;

__attribute__((section(".head.text"), noinline, used))
volatile unsigned long *p49_head_helper(volatile unsigned long *pointer) {
    return pointer;
}

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_head_entry(unsigned long value) {
    volatile unsigned long *volatile saved;
    p49_head_global = value;
    saved = p49_head_helper(&p49_head_global);
    return (unsigned long)saved;
}
