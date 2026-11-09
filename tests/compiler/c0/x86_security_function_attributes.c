/* Native Linux x86 function-security annotations have target-specific code
 * generation semantics. Preserve exact spellings and fail closed when the
 * policy is not one of MiniC's explicitly qualified fallback forms. */
#if defined(REJECT_NOCF_ARGUMENT)
__attribute__((nocf_check(1))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_INDIRECT_THUNK)
__attribute__((__indirect_branch__("thunk"))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_INDIRECT_THUNK_EXTERN)
__attribute__((__indirect_branch__("thunk-extern"))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_INDIRECT_NUMBER)
__attribute__((__indirect_branch__(0))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_TARGET_VECTOR)
__attribute__((__target__("avx2"))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_TARGET_ABI)
__attribute__((__target__("general-regs-only"))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_TARGET_NUMBER)
__attribute__((__target__(42))) int x86_rejected(int n) { return n; }
#elif defined(REJECT_TARGET_MIXED)
__attribute__((__target__("bmi2,avx2"))) int x86_rejected(int n) { return n; }
#else
__attribute__((nocf_check)) int x86_no_endbr(int n) { return n + 1; }
__attribute__((__indirect_branch__("keep")))
int x86_keep_branch(int (*callback)(int), int n) { return callback(n); }
__attribute__((__target__("bmi2")))
int x86_bmi2_fallback(int n) { return n * 13; }
/* Zstd's actual Linux 6.6 BMI2_TARGET_ATTRIBUTE, not a synthesized option. */
__attribute__((__target__("lzcnt,bmi,bmi2")))
int x86_zstd_bmi2_fallback(int n) { return n * 7; }
__attribute__((__target__("default")))
int x86_default_fallback(int n) { return n ^ 13; }
int x86_security_probe(int n) {
    return x86_keep_branch(x86_no_endbr, n) + x86_bmi2_fallback(n) +
           x86_zstd_bmi2_fallback(n) + x86_default_fallback(n);
}
#endif
