/* Independent GNU C driver: every assertion is a guest exit code. */
extern int minic_asm_signed_char(void);
extern int minic_asm_signed_short(void);
extern int minic_asm_unsigned_char(void);
extern int minic_asm_unsigned_short(void);
#if ABI_RV64
extern unsigned long minic_caller_arg_u32(void);
extern unsigned long minic_caller_stack_u32(void);
extern unsigned long abi_raw_call_ret_u32(void);
extern unsigned long minic_callee_convert_u32(unsigned int);
extern int minic_asm_unsigned32_readwrite(void);
extern int minic_asm_unsigned32_input(void);
extern int minic_asm_unsigned32_mixed(void);
extern int minic_asm_unsigned32_mixed_readwrite(void);
#endif
int main(void) {
    if (minic_asm_signed_char() != 1) return 11;
    if (minic_asm_signed_short() != 1) return 12;
    if (minic_asm_unsigned_char() != 1) return 13;
    if (minic_asm_unsigned_short() != 1) return 14;
#if ABI_RV64
    if (minic_caller_arg_u32() != 0xffffffff80000000UL) return 21;
    if (minic_caller_stack_u32() != 0xffffffff80000000UL) return 22;
    if (abi_raw_call_ret_u32() != 0xffffffff80000000UL) return 23;
    if (minic_callee_convert_u32(0x80000000U) != 0x80000000UL) return 24;
    if (minic_asm_unsigned32_readwrite() != 1) return 31;
    if (minic_asm_unsigned32_input() != 1) return 32;
    if (minic_asm_unsigned32_mixed() != 1) return 33;
    if (minic_asm_unsigned32_mixed_readwrite() != 1) return 34;
#endif
    return 0;
}
