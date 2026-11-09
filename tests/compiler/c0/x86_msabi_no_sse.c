/* A genuine MS x64 entry with a SysV outgoing call, as in EFI libstub.
 * With -mno-sse neither ABI boundary may use XMM save/restore opcodes. */
typedef unsigned long mini_u64;

__attribute__((noinline))
mini_u64 minic_ms_no_sse_helper(mini_u64 value) {
    return (value << 3) ^ 0x55UL;
}

__attribute__((ms_abi, noinline))
mini_u64 minic_ms_no_sse_entry(mini_u64 a, mini_u64 b) {
    return minic_ms_no_sse_helper(a) + b;
}

#ifdef MINIC_MSABI_FLOAT_NEGATIVE
__attribute__((ms_abi, noinline))
double minic_ms_no_sse_float(double value) {
    return value + 1.0;
}

int main(void) {
    return (int)minic_ms_no_sse_float(2.0);
}
#else
int main(void) {
    return minic_ms_no_sse_entry(7UL, 11UL) ==
        (((7UL << 3) ^ 0x55UL) + 11UL) ? 0 : 1;
}
#endif
