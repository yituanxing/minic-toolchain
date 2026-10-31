/* GNU/Linux EFI exposes real Microsoft-x64 function definitions, not just
 * foreign callbacks. These definitions are compiled once by GCC and once
 * by MiniC while the caller is always independent GCC machine code. */
extern long sysv_intrude(long value);
/* Linux EFI declares efi_pe_entry before its later body; exercise that
 * exact prototype-to-definition ABI metadata lifecycle. */
__attribute__((ms_abi))
long win64_scalar_entry(long, long, long, long, long, long);

__attribute__((ms_abi))
long win64_scalar_entry(long a, long b, long c, long d, long e, long f) {
    return sysv_intrude(e) + a * 3 + b * 5 + c * 7 + d * 11 + f * 13;
}

__attribute__((ms_abi))
double win64_mixed_entry(long a, double b, float c, long d, double e, long f) {
    return (double)a + b + (double)c + (double)d + e + (double)f;
}

__attribute__((ms_abi))
float win64_float_entry(float value) {
    return value + 1.25f;
}

__attribute__((ms_abi))
long win64_empty_entry(void) {
    return 0x1234;
}
