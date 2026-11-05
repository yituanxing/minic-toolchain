/* RV32/RV64 native C->Core->Machine psABI scalar register differential.
 * Cross-compile this exact file with both GNU C and MiniC. */
#if ABI_RV64
extern unsigned long abi_raw_arg_u32(unsigned int);
extern unsigned long abi_raw_stack_u32(
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned int);

unsigned long minic_caller_arg_u32(void) {
    unsigned int value = 0x80000000U;
    return abi_raw_arg_u32(value);
}
unsigned long minic_caller_stack_u32(void) {
    unsigned int value = 0x80000000U;
    return abi_raw_stack_u32(1UL, 2UL, 3UL, 4UL,
        5UL, 6UL, 7UL, 8UL, value);
}
unsigned int minic_ret_u32(void) {
    return 0x80000000U;
}
unsigned long minic_callee_convert_u32(unsigned int value) {
    /* GNU caller supplies sign-extended a0; Core must canonicalize
     * unsigned 32-bit values to zero-extended 64-bit C semantics. */
    return (unsigned long)value;
}
#endif

int minic_asm_signed_char(void) {
    signed char value = -2;
    __asm__ volatile ("srai %0,%0,1" : "+r"(value));
    return value == -1;
}
int minic_asm_signed_short(void) {
    signed short value = -2;
    __asm__ volatile ("srai %0,%0,1" : "+r"(value));
    return value == -1;
}
int minic_asm_unsigned_char(void) {
    unsigned char value = 0xfeU;
    __asm__ volatile ("srli %0,%0,1" : "+r"(value));
    return value == 0x7fU;
}
int minic_asm_unsigned_short(void) {
    unsigned short value = 0xfffeU;
    __asm__ volatile ("srli %0,%0,1" : "+r"(value));
    return value == 0x7fffU;
}
#if ABI_RV64
int minic_asm_unsigned32_readwrite(void) {
    unsigned int value = 0x80000000U;
    __asm__ volatile ("srai %0,%0,32" : "+r"(value));
    return value == 0xffffffffU;
}
int minic_asm_unsigned32_input(void) {
    unsigned int value = 0x80000000U;
    unsigned long output;
    __asm__ volatile ("srai %0,%1,32" : "=r"(output) : "r"(value));
    return output == ~0UL;
}
int minic_asm_unsigned32_mixed(void) {
    unsigned int value = 0x80000000U;
    unsigned long output;
    __asm__ volatile ("srai %0,%3,32" : "=&r"(output)
                      : "r"(value), "r"(value), "r"(value));
    return output == ~0UL;
}
int minic_asm_unsigned32_mixed_readwrite(void) {
    unsigned int value = 0x80000000U;
    __asm__ volatile ("srai %0,%0,32" : "+r"(value)
                      : "r"(value), "r"(value));
    return value == 0xffffffffU;
}
#endif
