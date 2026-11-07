/* GCC oracle and MiniC LoongArch GNU-alias ABI test vectors. Compile each
 * `-DCASE=N` through the host preprocessor before passing the resulting .i
 * to MiniC; a malformed GNU alias does not prove target-policy rejection. */
#ifndef CASE
#define CASE 0
#endif

#if CASE == 0
/* Genuine Linux __SYSCALL_DEFINEx ABI transport: two 64-bit GPR words. */
extern long sys_entry(unsigned int id, const char *ptr)
    __attribute__((alias("__se_sys_entry")));
long __se_sys_entry(long id, long ptr) { return id + (ptr != 0); }
int main(void) { return sys_entry(41U, (const char *)1) == 42 ? 0 : 1; }
#elif CASE == 1
/* Generic C-compatible aliases remain legal regardless of symbol spelling. */
extern long ordinary_alias(long n) __attribute__((alias("ordinary_owner")));
long ordinary_owner(long n) { return n + 1; }
int main(void) { return ordinary_alias(41) == 42 ? 0 : 1; }
#elif CASE == 2
/* An unrelated alias must not inherit sys_*'s ABI exception. */
extern long ordinary_alias(unsigned int id) __attribute__((alias("ordinary_owner")));
long ordinary_owner(long id) { return id; }
#elif CASE == 3
/* The two symbol suffixes must match exactly. */
extern long sys_alpha(unsigned int id) __attribute__((alias("__se_sys_beta")));
long __se_sys_beta(long id) { return id; }
#elif CASE == 4
/* COMPAT syscalls are a separate Linux ABI family, not admitted implicitly. */
extern long compat_sys_entry(unsigned int id)
    __attribute__((alias("__se_compat_sys_entry")));
long __se_compat_sys_entry(long id) { return id; }
#elif CASE == 5
/* FP arguments travel through a different LoongArch ABI register class. */
extern long sys_float(float value) __attribute__((alias("__se_sys_float")));
long __se_sys_float(long value) { return value; }
#elif CASE == 6
/* Aggregate arguments can use a different ABI transfer shape. */
struct Word { int value; };
extern long sys_record(struct Word value) __attribute__((alias("__se_sys_record")));
long __se_sys_record(long value) { return value; }
#elif CASE == 7
/* A variadic alias cannot use the fixed-arity syscall sanitizer contract. */
extern long sys_vararg(int value, ...) __attribute__((alias("__se_sys_vararg")));
long __se_sys_vararg(long value, ...) { return value; }
#elif CASE == 8
/* A 128-bit source parameter needs more than one GPR word. */
extern long sys_wide(__int128 value) __attribute__((alias("__se_sys_wide")));
long __se_sys_wide(long value) { return value; }
#elif CASE == 9
/* The sanitizer must receive a full 64-bit long/long long word. */
extern long sys_small(int value) __attribute__((alias("__se_sys_small")));
long __se_sys_small(unsigned int value) { return value; }
#elif CASE == 10
/* Source and sanitizer return types must be C-compatible signed long. */
extern int sys_result(int value) __attribute__((alias("__se_sys_result")));
long __se_sys_result(long value) { return value; }
#elif CASE == 11
/* Linux __SYSCALL_DEFINEx admits at most six positional arguments. */
extern long sys_seven(int a, int b, int c, int d, int e, int f, int g)
    __attribute__((alias("__se_sys_seven")));
long __se_sys_seven(long a, long b, long c, long d, long e, long f, long g) {
    return a + b + c + d + e + f + g;
}
#elif CASE == 12
/* A 64-bit long long source travels to __SC_LONG's signed long long word. */
extern long sys_longlong(unsigned long long value)
    __attribute__((alias("__se_sys_longlong")));
long __se_sys_longlong(long long value) { return value == 41 ? 42 : 0; }
int main(void) { return sys_longlong(41ULL) == 42 ? 0 : 1; }
#else
#error unknown test case
#endif
