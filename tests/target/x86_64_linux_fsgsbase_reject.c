/* Target-qualified Linux FSGSBASE negative cases. Each mode must fail MiniC
 * -S before GNU AS: mismatched constraints, Source spelling, scalar width or
 * compiler memory ordering must never become an unchecked physical opcode. */
typedef unsigned long u64;

#if MINIC_FSGSBASE_NEGATIVE == 1
u64 wrong_read_output(void) {
    u64 value;
    asm volatile("rdfsbase %0" : "=m" (value) : : "memory");
    return value;
}
#elif MINIC_FSGSBASE_NEGATIVE == 2
void wrong_write_input(u64 value) {
    asm volatile("wrfsbase %0" : : "m" (value) : "memory");
}
#elif MINIC_FSGSBASE_NEGATIVE == 3
u64 forged_extra_instruction(void) {
    u64 value;
    asm volatile("rdfsbase %0; nop" : "=r" (value) : : "memory");
    return value;
}
#elif MINIC_FSGSBASE_NEGATIVE == 4
unsigned int narrowed_result(void) {
    unsigned int value;
    asm volatile("rdfsbase %0" : "=r" (value) : : "memory");
    return value;
}
#elif MINIC_FSGSBASE_NEGATIVE == 5
u64 missing_memory_barrier(void) {
    u64 value;
    asm volatile("rdgsbase %0" : "=r" (value));
    return value;
}
#else
#error unsupported FSGSBASE negative mode
#endif
