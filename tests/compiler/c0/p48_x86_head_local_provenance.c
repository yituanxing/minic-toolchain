/* MC-076: all heads use the SAME early-boot C source as the GNU controls.
 * The code and .head.data objects are moved together at runtime.  Pointer
 * storage in a local Core object must not erase the distinction between the
 * displaced physical dereference and the original link-time numeric VMA.
 * No C-source or target flag is used to mark the desired address role. */

volatile unsigned long p48_head_cells[3]
    __attribute__((section(".head.data"))) = {0x11UL, 0x22UL, 0x33UL};
volatile unsigned long p48_head_other
    __attribute__((section(".head.data"))) = 0x44UL;

/* GCC -O2 independently produces PC-relative store + link-VMA return. */
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_baseline(unsigned long value, unsigned long selector) {
    (void)selector;
    p48_head_cells[0] = value;
    return (unsigned long)&p48_head_cells[0];
}

/* Deliberately non-volatile local pointer: GCC -O2 can coalesce this into a
 * PC-relative physical store and absolute link-VMA return.  MiniC still
 * has to preserve the same two observable behaviors. */
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_plain_local(unsigned long value,
                                   unsigned long selector) {
    volatile unsigned long *pointer = &p48_head_cells[1];
    (void)selector;
    *pointer = value;
    return (unsigned long)pointer;
}

/* Volatile pointer OBJECT storage forces a genuine pointer STORE/LOAD. */
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_local(unsigned long value, unsigned long selector) {
    volatile unsigned long *volatile pointer = &p48_head_cells[0];
    (void)selector;
    *pointer = value;
    return (unsigned long)pointer;
}

/* Transport through two independent local pointer slots.  The pointer
 * offset is runtime-variable, followed by pointer->integer->pointer; the
 * returned integer must still be a link VMA including that same offset. */
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_roundtrip(unsigned long value, unsigned long selector) {
    volatile unsigned long *volatile pointer = &p48_head_cells[0];
    unsigned long numeric = (unsigned long)(pointer + selector);
    volatile unsigned long *volatile roundtrip =
        (volatile unsigned long *)numeric;
    *roundtrip = value;
    return (unsigned long)roundtrip;
}

/* Two control-flow edges agree on the SAME symbol but select different
 * elements.  A proof may retain the shared origin, not a fixed offset. */
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_same_origin_branch(unsigned long value,
                                          unsigned long selector) {
    volatile unsigned long *volatile pointer = &p48_head_cells[0];
    if (selector != 0UL)
        pointer = &p48_head_cells[1];
    *pointer = value;
    return (unsigned long)pointer;
}

/* A pointer lives in an addressable record field.  This is a distinct Core
 * field-address LOAD/STORE path, not the scalar local-slot fast path. */
struct P48PointerRecord {
    unsigned long tag;
    volatile unsigned long *pointer;
};

__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_record_field(unsigned long value,
                                    unsigned long selector) {
    struct P48PointerRecord record;
    (void)selector;
    record.tag = 0x17UL;
    record.pointer = &p48_head_cells[2];
    *record.pointer = value;
    return (unsigned long)record.pointer;
}

/* A fixed result-address inside __builtin_add_overflow is a real machine
 * memory writer, even though it is not a Core STORE instruction.  Use a
 * runtime selector (zero in this particular oracle) to keep the overflow
 * operation live, then verify the displaced *result address* was written. */
__attribute__((section(".head.text"), noinline, used))
unsigned long p48_head_overflow_address(unsigned long value,
                                        unsigned long selector) {
    unsigned int carry = __builtin_add_overflow(
        value, selector, (unsigned long *)&p48_head_cells[2]);
    return carry ? 0UL : (unsigned long)&p48_head_cells[2];
}

/* Ordinary .text runs at its linked address and must not inherit the early
 * physical/link-VMA conversion.  The PIE variant uses this same source. */
__attribute__((noinline, used))
unsigned long p48_normal_local(unsigned long value, unsigned long selector) {
    volatile unsigned long *volatile pointer = &p48_head_cells[0];
    (void)selector;
    *pointer = value;
    return (unsigned long)pointer;
}
