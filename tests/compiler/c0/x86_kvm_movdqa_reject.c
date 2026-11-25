/* KVM's exact vector_size(16) 16-byte aligned MOVDQA contract is not
 * permitted to admit byte-sized memory, another vector width, or unsupported
 * registers/constraints. All cases are intentional MiniC failures. */
typedef unsigned int u32;
typedef u32 __attribute__((vector_size(16))) v128;
typedef u32 __attribute__((vector_size(8))) v64;
static unsigned char scalar[16] __attribute__((aligned(16)));
void reject(void) {
#if BAD_FORM == 1
    asm("movdqa %%xmm0,%0" : "=m"(scalar[0]));
#elif BAD_FORM == 2
    v64 wrong;
    asm("movdqa %%xmm0,%0" : "=m"(wrong));
#elif BAD_FORM == 3
    v128 right;
    asm("movdqa %%xmm16,%0" : "=m"(right));
#elif BAD_FORM == 4
    v128 right;
    asm("movdqa %%xmm0,%0" : "=r"(right));
#elif BAD_FORM == 5
    const v128 right = {0, 0, 0, 0};
    asm("movdqa %%xmm0,%0" : "=m"(right));
#endif
}
