/* Exact Linux arch/x86/kvm/fpu.h movdqa GNU memory constraints.
 * The kernel compiles -mno-sse: only explicit asm may touch XMM. */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u32 __attribute__((vector_size(16))) sse128_t;
union kvm_xmm_image { sse128_t vec; u64 words[2]; u32 lanes[4]; };

#define DECLARE_KVM_XMM(N) \
    __attribute__((noinline)) void kvm_write_xmm##N(const sse128_t *p) { \
        asm("movdqa %0, %%xmm" #N :: "m"(*p)); \
    } \
    __attribute__((noinline)) void kvm_read_xmm##N(sse128_t *p) { \
        asm("movdqa %%xmm" #N ", %0" : "=m"(*p)); \
    }

DECLARE_KVM_XMM(0)
DECLARE_KVM_XMM(1)
DECLARE_KVM_XMM(2)
DECLARE_KVM_XMM(3)
DECLARE_KVM_XMM(4)
DECLARE_KVM_XMM(5)
DECLARE_KVM_XMM(6)
DECLARE_KVM_XMM(7)
DECLARE_KVM_XMM(8)
DECLARE_KVM_XMM(9)
DECLARE_KVM_XMM(10)
DECLARE_KVM_XMM(11)
DECLARE_KVM_XMM(12)
DECLARE_KVM_XMM(13)
DECLARE_KVM_XMM(14)
DECLARE_KVM_XMM(15)

/* A normal scalar/union write of all 16 bytes is required in addition to
 * direct XMM memory transfer. C by-value vector CALL is deliberately absent. */
int main(void) {
    union kvm_xmm_image a, b;
    a.words[0] = 0x123456789abcdef0ULL;
    a.words[1] = 0xfedcba9876543210ULL;
    b.words[0] = 0;
    b.words[1] = 0;
    kvm_write_xmm0(&a.vec);
    kvm_read_xmm0(&b.vec);
    if (b.words[0] != a.words[0] || b.words[1] != a.words[1])
        return 1;
    b.words[0] = 0;
    b.words[1] = 0;
    kvm_write_xmm15(&a.vec);
    kvm_read_xmm15(&b.vec);
    return b.words[0] != a.words[0] || b.words[1] != a.words[1];
}
