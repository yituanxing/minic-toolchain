/* MC-076 P48 negative admission: two distinct unproved local-pointer
 * origins, each also used in a function with a direct physical data store.
 * A target must never silently assign one global's physical/VMA policy to
 * a pointer after conflicting writes or an address-escaping local slot.
 * `P48_CASE_CONFLICT` and `P48_CASE_ALIAS` are compiled independently so a
 * rejection in one cannot hide accidental acceptance of the other. */

volatile unsigned long p48_uncertain_cells[2]
    __attribute__((section(".head.data"))) = {0x11UL, 0x22UL};
volatile unsigned long p48_uncertain_other
    __attribute__((section(".head.data"))) = 0x33UL;

#ifdef P48_CASE_CONFLICT
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_uncertain_conflict(unsigned long value,
                                     unsigned long selector) {
    volatile unsigned long *volatile pointer = &p48_uncertain_cells[0];
    if (selector != 0UL)
        pointer = &p48_uncertain_other;
    p48_uncertain_cells[0] = value;
    return (unsigned long)pointer;
}
#endif

#ifdef P48_CASE_ALIAS
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_uncertain_escaped_alias(unsigned long value,
                                          unsigned long selector) {
    volatile unsigned long *volatile pointer = &p48_uncertain_cells[0];
    volatile unsigned long *volatile *alias = &pointer;
    if (selector != 0UL)
        *alias = &p48_uncertain_other;
    p48_uncertain_cells[0] = value;
    return (unsigned long)pointer;
}
#endif
