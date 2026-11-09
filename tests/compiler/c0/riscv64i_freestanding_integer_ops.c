/* Real RV64I Linux needs arithmetic which never imports libgcc symbols.
 * Keep these functions reachable: GNU Kbuild links the original operations
 * from 2064 independent C owners without a hosted libgcc runtime. */
typedef unsigned long long u64;
typedef long long s64;
typedef unsigned __int128 u128;
typedef __int128 s128;

u64 rv64i_linux_umul(u64 a, u64 b) { return a * b; }
u64 rv64i_linux_udiv(u64 a, u64 b) { return a / b; }
u64 rv64i_linux_umod(u64 a, u64 b) { return a % b; }
s64 rv64i_linux_smul(s64 a, s64 b) { return a * b; }
s64 rv64i_linux_sdiv(s64 a, s64 b) { return a / b; }
s64 rv64i_linux_smod(s64 a, s64 b) { return a % b; }
s128 rv64i_linux_multi3(s128 a, s128 b) { return a * b; }
u128 rv64i_linux_umulti3(u128 a, u128 b) { return a * b; }

/* Actual Linux io_uring / fbcon checked-overflow operand shapes. */
int rv64i_linux_mixed_mul(int signed_input, unsigned int unsigned_input,
                          int *result) {
    return __builtin_mul_overflow(signed_input, unsigned_input, result);
}
int rv64i_linux_checked_add(unsigned long wide, int narrow, int *result) {
    return __builtin_add_overflow(wide, narrow, result);
}

/* C makes INT64_MIN / -1 undefined; the backend must nonetheless not depend
 * on a libgcc helper for its division sequence. Compile this shape, but do
 * not execute the undefined arithmetic pair in the runtime oracle. */
s64 rv64i_linux_div_min_candidate(s64 divisor) {
    return (-9223372036854775807LL - 1LL) / divisor;
}

int main(void) {
    volatile u64 dividend = 0xfedcba9876543210ULL;
    volatile u64 divisor = 123456789ULL;
    volatile s64 signed_dividend = -777777777777LL;
    volatile s64 signed_divisor = 12345LL;
    u64 quotient = rv64i_linux_udiv(dividend, divisor);
    u64 remainder = rv64i_linux_umod(dividend, divisor);
    s64 signed_quotient = rv64i_linux_sdiv(signed_dividend, signed_divisor);
    s64 signed_remainder = rv64i_linux_smod(signed_dividend, signed_divisor);
    int checked = 0;
    if (quotient * divisor + remainder != dividend || remainder >= divisor)
        return 1;
    if (signed_quotient * signed_divisor + signed_remainder != signed_dividend)
        return 2;
    if (signed_remainder >= 0 || signed_remainder <= -signed_divisor)
        return 3;
    if (rv64i_linux_umul(0xffffffffffffffffULL, 3ULL) !=
        0xfffffffffffffffdULL)
        return 4;
    if (rv64i_linux_smul(-17LL, 19LL) != -323LL)
        return 5;
    if (rv64i_linux_multi3((s128)-1234567, (s128)9876543) !=
        (s128)-12193254061881LL)
        return 6;
    if (rv64i_linux_umulti3((u128)1234567, (u128)9876543) !=
        (u128)12193254061881ULL)
        return 7;
    if (rv64i_linux_mixed_mul(-2, 3U, &checked) || checked != -6)
        return 8;
    if (!rv64i_linux_checked_add(~0UL, 1, &checked) || checked != 0)
        return 9;
    if (rv64i_linux_sdiv((-9223372036854775807LL - 1LL), 2) !=
        -4611686018427387904LL)
        return 10;
    return 42;
}

#ifdef RISCV_GUEST
__asm__(".globl _start\n"
        "_start:\n"
        " call main\n"
        " li a7, 93\n"
        " ecall\n");
#endif
