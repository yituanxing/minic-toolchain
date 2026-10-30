/* Linux 6.6.143 arch/arm64/include/asm/kvm_host.h __build_check_flag:
 * __fls is static-inline, not a lexically constant expression; GCC -O2
 * specializes the mask and proves the bound at the concrete callsite. */
#define BUILD_BUG_ON(expr) do { \
    extern void __attribute__((error("invalid KVM flag"))) __kvm_flag_error(void); \
    if (expr) __kvm_flag_error(); \
} while (0)
static __attribute__((always_inline)) inline unsigned long
__fls(unsigned long word) {
    return (sizeof(word) * 8) - 1 - __builtin_clzl(word);
}
static __attribute__((always_inline)) inline void check_flag(unsigned long mask) {
    BUILD_BUG_ON((sizeof(unsigned long) * 8) <= __fls(mask));
}
void call_check(void) {
    check_flag(1UL << 4);
}

/* Keep an observable runtime call in a different function. The constant
 * return evaluator must never treat an impure/volatile callee as a pure fact. */
extern volatile unsigned long runtime_flag;
extern void runtime_only(void);
static inline unsigned long read_runtime_flag(void) {
    return runtime_flag;
}
void preserve_runtime_guard(void) {
    if (read_runtime_flag()) runtime_only();
}

/* clz(0) is undefined and must not become a speculative constant. */
extern void zero_operand_guard(void);
void preserve_undefined_zero(void) {
    if (__builtin_clzl(0UL) == 64U) zero_operand_guard();
}
