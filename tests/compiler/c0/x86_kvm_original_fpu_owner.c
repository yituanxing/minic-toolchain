/* Compile the UNMODIFIED Linux 6.6.143 arch/x86/kvm/fpu.h through a tiny
 * kernel-API shim: neither the vector typedef nor the 32 XMM asm patterns
 * are pasted/reimplemented here. LINUX_SRC is supplied by the harness. */
typedef unsigned int u32;
typedef unsigned long long u64;
extern void abort(void);
#define CONFIG_X86_64 1
#define BUG() abort()
#define fpregs_lock() ((void)0)
#define fpregs_unlock() ((void)0)
#define fpregs_assert_state_consistent() ((void)0)
#define TIF_NEED_FPU_LOAD 0
#define test_thread_flag(flag) 0
#define switch_fpu_return() ((void)0)
#include "arch/x86/kvm/fpu.h"

/* Variable-index helpers force all 16 register arms in the original source
 * to stay eligible; dynamic runtime below checks actual XMM0 and XMM15. */
__attribute__((noinline)) void kvm_original_write(int reg, const sse128_t *p) {
    _kvm_write_sse_reg(reg, p);
}
__attribute__((noinline)) void kvm_original_read(int reg, sse128_t *p) {
    _kvm_read_sse_reg(reg, p);
}

int main(void) {
    __sse128_u input, output;
    input.as_u64[0] = 0x123456789abcdef0ULL;
    input.as_u64[1] = 0xfedcba9876543210ULL;
    output.as_u64[0] = 0;
    output.as_u64[1] = 0;
    kvm_original_write(0, &input.vec);
    kvm_original_read(0, &output.vec);
    if (output.as_u64[0] != input.as_u64[0] ||
        output.as_u64[1] != input.as_u64[1]) return 1;
    output.as_u64[0] = 0;
    output.as_u64[1] = 0;
    kvm_original_write(15, &input.vec);
    kvm_original_read(15, &output.vec);
    if (output.as_u64[0] != input.as_u64[0] ||
        output.as_u64[1] != input.as_u64[1]) return 2;
    /* Linux hyperv.c and fpu.h use this exact GNU statement-expression
     * union transport, which must preserve both 64-bit lanes with -mno-sse. */
    if (sse128_lo(input.vec) != input.as_u64[0] ||
        sse128_hi(input.vec) != input.as_u64[1]) return 3;
    return 0;
}
