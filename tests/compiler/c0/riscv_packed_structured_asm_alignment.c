/* GNU constraints are source owned; the compiler-generated register ingress
 * and egress must obey the same packed-lvalue alignment as ordinary C.
 * Native GNU x86 uses x86 template spelling; RISCV_ASM_PROBE_TARGET selects
 * the equivalent Base-I source text for RV32I and RV64I -S verification. */
#ifdef RISCV_ASM_PROBE_TARGET
#define ASM_INT_INC "addi %0, %0, 1"
#define ASM_INT_SET "li %0, 17"
#define ASM_LONG_INC "addi %0, %0, 1"
#else
#define ASM_INT_INC "addl $1, %0"
#define ASM_INT_SET "movl $17, %0"
#define ASM_LONG_INC "addq $1, %0"
#endif

struct __attribute__((packed)) PackedAsm {
    unsigned char prefix;
    int value;
    long wide;
};

void packed_asm_readwrite(struct PackedAsm *p) {
    __asm__ __volatile__(ASM_INT_INC : "+r"(p->value));
}

void packed_asm_output(struct PackedAsm *p) {
    __asm__ __volatile__(ASM_INT_SET : "=r"(p->value));
}

void packed_asm_wide(struct PackedAsm *p) {
    __asm__ __volatile__(ASM_LONG_INC : "+r"(p->wide));
}

/* Six simultaneous operands survive a large-displacement frame reload. The
 * static frame transport may only use t2, t5 and t6 as hidden temporaries;
 * those registers must never be allocated as any asm input at this site. */
void packed_asm_large_frame(long a, long b, long c,
                            long d, long e, long f) {
    volatile unsigned char space[8192];
    space[0] = 1;
    (void)space[0];
    __asm__ __volatile__(
        "# MINIC_PACKED_ASM_INPUTS %0 %1 %2 %3 %4 %5"
        : : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f)
        : "memory");
}

#ifndef RISCV_ASM_PROBE_TARGET
int main(void) {
    struct PackedAsm p;
    p.prefix = 71;
    p.value = 4;
    p.wide = 123;
    packed_asm_readwrite(&p);
    if (p.value != 5)
        return 1;
    packed_asm_output(&p);
    if (p.value != 17)
        return 2;
    packed_asm_wide(&p);
    if (p.wide != 124 || p.prefix != 71)
        return 3;
    packed_asm_large_frame(1, 2, 3, 4, 5, 6);
    return 0;
}
#endif
