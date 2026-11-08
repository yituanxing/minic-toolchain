/* Distinct positive admission: a head helper physically dereferences the
 * pointer and never treats it as a numeric link-VMA. Do not reject all
 * pointer arguments merely because a global is also directly accessed. */
volatile unsigned long p49_physical_global
    __attribute__((section(".head.data"))) = 0x12UL;

__attribute__((section(".head.text"), noinline, used))
void p49_physical_helper(volatile unsigned long *pointer,
                         unsigned long value) {
    *pointer = value;
}

__attribute__((section(".head.text"), noinline, used))
unsigned long p49_physical_entry(unsigned long value) {
    p49_physical_global = value;
    p49_physical_helper(&p49_physical_global, value + 1UL);
    return p49_physical_global;
}
