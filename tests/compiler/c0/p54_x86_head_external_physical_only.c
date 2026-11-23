/* MC-076/P54 positive control: an external helper only consumes a pointer
 * for an early physical access.  No pointer return is interpreted as a
 * numerical link-VMA value.  It must remain compile-admissible. */
volatile unsigned long p54_physical_global
    __attribute__((section(".head.data"))) = 0x2211UL;

extern void p54_external_physical_writer(volatile unsigned long *pointer);

__attribute__((section(".head.text"), noinline, used))
void p54_head_entry(unsigned long value) {
    p54_physical_global = value;
    p54_external_physical_writer(&p54_physical_global);
}
