/* Linux 6.6 arch/x86/kvm/fpu.h:8 -- GNU fixed vector *storage* with
 * explicit -mno-sse kernel flags. No SIMD arithmetic or call ABI inferred. */
typedef unsigned int u32;
typedef u32 __attribute__((vector_size(16))) sse128_t;
typedef u32 __attribute__((__vector_size__(8))) sse64_t;

_Static_assert(sizeof(sse128_t) == 16, "KVM XMM image must be 16 bytes");
_Static_assert(_Alignof(sse128_t) == 16, "movdqa requires 16-byte alignment");
_Static_assert(sizeof(sse64_t) == 8, "distinct GNU vector width");
_Static_assert(_Alignof(sse64_t) == 8, "8-byte vector alignment");

struct kvm_vector_wrapper {
    unsigned char marker;
    sse128_t state;
    unsigned char tail;
};
_Static_assert(__builtin_offsetof(struct kvm_vector_wrapper, state) == 16,
               "vector field alignment");
_Static_assert(sizeof(struct kvm_vector_wrapper) == 48,
               "vector member controls struct stride");

union kvm_vector_image {
    sse128_t vec;
    unsigned long long as_u64[2];
    u32 as_u32[4];
};
_Static_assert(sizeof(union kvm_vector_image) == 16, "union full XMM image");
_Static_assert(_Alignof(union kvm_vector_image) == 16, "union XMM alignment");

unsigned long kvm_vector_storage_signature(void) {
    return (unsigned long)(sizeof(struct kvm_vector_wrapper) +
                           __builtin_offsetof(struct kvm_vector_wrapper, state) +
                           _Alignof(union kvm_vector_image));
}
