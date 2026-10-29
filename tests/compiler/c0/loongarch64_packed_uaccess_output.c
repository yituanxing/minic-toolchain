/* GNU's genuine LoongArch __get_user fault-table form. Packed destinations
 * exercise both the +r error ingress/egress and =r data carrier transport. */
#define LOONG_GPR_MAP \
    ".equ .L__gpr_num_zero, 0\n" \
    ".irp num, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n" \
    ".equ .L__gpr_num_$r\\num, \\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_$ra, 1\n" \
    ".equ .L__gpr_num_$tp, 2\n" \
    ".equ .L__gpr_num_$sp, 3\n" \
    ".irp num, 0,1,2,3,4,5,6,7\n" \
    ".equ .L__gpr_num_$a\\num, 4+\\num\n" \
    ".endr\n" \
    ".irp num, 0,1,2,3,4,5,6,7,8\n" \
    ".equ .L__gpr_num_$t\\num, 12+\\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_$s9, 22\n" \
    ".equ .L__gpr_num_$fp, 22\n" \
    ".irp num, 0,1,2,3,4,5,6,7,8\n" \
    ".equ .L__gpr_num_$s\\num, 23+\\num\n" \
    ".endr\n"

#define LOONG_UACCESS(OPCODE) \
    "1: " OPCODE " %1, %2\n2:\n" LOONG_GPR_MAP \
    ".pushsection __ex_table, \"a\"\n" \
    ".balign 4\n" \
    ".long ((1b) - .)\n" \
    ".long ((2b) - .)\n" \
    ".short (2)\n" \
    ".short ((((.L__gpr_num_%0) << 0) | ((.L__gpr_num_%1) << 5)))\n" \
    ".popsection\n"

struct __attribute__((packed)) PackedUser {
    unsigned char pad;
    int err;
    unsigned char byte;
    unsigned short half;
    unsigned int word;
    unsigned long canary;
};

void packed_uaccess_byte(struct PackedUser *r, const unsigned char *ptr) {
    __asm__ __volatile__(LOONG_UACCESS("ld.b")
        : "+r"(r->err), "=r"(r->byte) : "m"(*ptr));
}
void packed_uaccess_half(struct PackedUser *r, const unsigned short *ptr) {
    __asm__ __volatile__(LOONG_UACCESS("ld.h")
        : "+r"(r->err), "=r"(r->half) : "m"(*ptr));
}
void packed_uaccess_word(struct PackedUser *r, const unsigned int *ptr) {
    __asm__ __volatile__(LOONG_UACCESS("ld.w")
        : "+r"(r->err), "=r"(r->word) : "m"(*ptr));
}

/* GNU's register C carrier can be larger than the faulting memory access.
 * The actual register output object is still eight bytes here. */
void packed_uaccess_wide_carrier(struct PackedUser *r,
                                const unsigned char *ptr) {
    __asm__ __volatile__(LOONG_UACCESS("ld.b")
        : "+r"(r->err), "=r"(r->canary) : "m"(*ptr));
}
