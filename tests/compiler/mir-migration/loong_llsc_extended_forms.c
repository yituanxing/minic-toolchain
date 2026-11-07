/* Exact GNU Linux LoongArch source forms: four sealed atomic/exception
 * families selected through typed physical MachineIR SOURCE_ASM bindings.
 * The source is preprocessed on the host; privileged forms are not executed. */
typedef unsigned int u32;
typedef unsigned long u64;

#define EXT_GPR_MAP \
    ".equ .L__gpr_num_zero,0\n" \
    ".irp num, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n" \
    ".equ .L__gpr_num_$r\\num,\\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_$ra,1\n" \
    ".equ .L__gpr_num_$tp,2\n" \
    ".equ .L__gpr_num_$sp,3\n" \
    ".irp num, 0,1,2,3,4,5,6,7\n" \
    ".equ .L__gpr_num_$a\\num,4+\\num\n" \
    ".endr\n" \
    ".irp num, 0,1,2,3,4,5,6,7,8\n" \
    ".equ .L__gpr_num_$t\\num,12+\\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_$s9,22\n" \
    ".equ .L__gpr_num_$fp,22\n" \
    ".irp num, 0,1,2,3,4,5,6,7,8\n" \
    ".equ .L__gpr_num_$s\\num,23+\\num\n" \
    ".endr\n"

#define EXT_FAULT(SITE) \
    EXT_GPR_MAP \
    ".pushsection __ex_table, \"a\"\n" \
    ".balign 4\n" \
    ".long ((" SITE ") - .)\n" \
    ".long ((3b) - .)\n" \
    ".short (2)\n" \
    ".short ((((.L__gpr_num_%0) << 0) | ((.L__gpr_num_zero) << 5)))\n" \
    ".popsection\n"

/* ID 7: `+r` err is a real read/write register and must not be reset by
 * the selector before the original source-owned exception fixups run. */
int loong_futex_cmpxchg(u32 *ptr, u32 expected, u32 desired) {
    /* `+r` must ingress this nonzero value; the old emitter lost it. */
    int err = 7;
    u32 prior;
    __asm__ __volatile__(
        "#futex_atomic_cmpxchg_inatomic\n"
        "1: ll.w %1, %3\n"
        "bne %1, %z4, 3f\n"
        "move $t0, %z5\n"
        "2: sc.w $t0, %2\n"
        "beqz $t0, 1b\n"
        "3: dbar 0x700\n"
        EXT_FAULT("1b") EXT_FAULT("2b")
        : "+r"(err), "=&r"(prior), "=ZC"(*ptr)
        : "ZC"(*ptr), "Jr"(expected), "Jr"(desired)
        : "t0", "memory");
    return err + (int)prior;
}

/* ID 18: two widths, both named and numeric form recognition is sealed. */
u32 loong_add_unless_w(volatile u32 *ptr, u32 delta, u32 unless) {
    u32 prior, result;
    __asm__ __volatile__(
        "0: ll.w %[p], %[c]\n"
        "beq %[p], %[u], 1f\n"
        "add.w %[rc], %[p], %[a]\n"
        "sc.w %[rc], %[c]\n"
        "beqz %[rc], 0b\n"
        "b 2f\n"
        "1: dbar 0x700\n"
        "2:"
        : [p] "=&r"(prior), [rc] "=&r"(result), [c] "=ZB"(*ptr)
        : [a] "r"(delta), [u] "r"(unless) : "memory");
    return prior;
}

u64 loong_add_unless_d(volatile u64 *ptr, u64 delta, u64 unless) {
    u64 prior, result;
    __asm__ __volatile__(
        "0: ll.d %0, %2\n"
        "beq %0, %4, 1f\n"
        "add.d %1, %0, %3\n"
        "sc.d %1, %2\n"
        "beqz %1, 0b\n"
        "b 2f\n"
        "1: dbar 0x700\n"
        "2:"
        : "=&r"(prior), "=&r"(result), "=ZB"(*ptr)
        : "r"(delta), "r"(unless) : "memory");
    return prior;
}

/* ID 19: all five exact futex RMW variants, one tied `0` input, two
 * physically distinct LL/SC memory operands and both Linux fault sites. */
#define FUTEX_RMW_TEMPLATE(OP) \
    "1: ll.w %1, %4 # __futex_atomic_op\n" \
    OP "\n" \
    "2: sc.w $t0, %2\n" \
    "beqz $t0, 1b\n" \
    "3:\n" \
    EXT_FAULT("1b") EXT_FAULT("2b")

#define DEFINE_FUTEX_RMW(NAME,OP) \
    int NAME(u32 *ptr, u32 input) { \
        int error, prior; \
        __asm__ __volatile__(FUTEX_RMW_TEMPLATE(OP) \
            : "=r"(error), "=&r"(prior), "=ZC"(*ptr) \
            : "0"(0), "ZC"(*ptr), "Jr"(input) \
            : "t0", "memory"); \
        return error + prior; \
    }

DEFINE_FUTEX_RMW(loong_futex_set, "move $t0, %z5")
DEFINE_FUTEX_RMW(loong_futex_add, "add.w $t0, %1, %z5")
DEFINE_FUTEX_RMW(loong_futex_or,  "or $t0, %1, %z5")
DEFINE_FUTEX_RMW(loong_futex_and, "and $t0, %1, %z5")
DEFINE_FUTEX_RMW(loong_futex_xor, "xor $t0, %1, %z5")

/* ID 23: true signed-s12 immediate ADD and register SUB, both widths. */
int loong_sub_positive_addi_w(volatile int *ptr) {
    int result, status;
    __asm__ __volatile__(
        "1: ll.w %1, %2 # atomic_sub_if_positive\n"
        "addi.w %0, %1, %3\n"
        "move %1, %0\n"
        "bltz %0, 2f\n"
        "sc.w %1, %2\n"
        "beqz %1, 1b\n"
        "2: dbar 0x700"
        : "=&r"(result), "=&r"(status), "+ZC"(*ptr)
        : "I"(-1));
    return result;
}

int loong_sub_positive_sub_w(volatile int *ptr, int decrement) {
    int result, status;
    __asm__ __volatile__(
        "1: ll.w %1, %2 # atomic_sub_if_positive\n"
        "sub.w %0, %1, %3\n"
        "move %1, %0\n"
        "bltz %0, 2f\n"
        "sc.w %1, %2\n"
        "beqz %1, 1b\n"
        "2: dbar 0x700"
        : "=&r"(result), "=&r"(status), "+ZC"(*ptr)
        : "r"(decrement));
    return result;
}

long loong_sub_positive_addi_d(volatile long *ptr) {
    long result, status;
    __asm__ __volatile__(
        "1: ll.d %1, %2 # atomic64_sub_if_positive\n"
        "addi.d %0, %1, %3\n"
        "move %1, %0\n"
        "bltz %0, 2f\n"
        "sc.d %1, %2\n"
        "beqz %1, 1b\n"
        "2: dbar 0x700"
        : "=&r"(result), "=&r"(status), "+ZC"(*ptr)
        : "I"(-1L));
    return result;
}

long loong_sub_positive_sub_d(volatile long *ptr, long decrement) {
    long result, status;
    __asm__ __volatile__(
        "1: ll.d %1, %2 # atomic64_sub_if_positive\n"
        "sub.d %0, %1, %3\n"
        "move %1, %0\n"
        "bltz %0, 2f\n"
        "sc.d %1, %2\n"
        "beqz %1, 1b\n"
        "2: dbar 0x700"
        : "=&r"(result), "=&r"(status), "+ZC"(*ptr)
        : "r"(decrement));
    return result;
}
