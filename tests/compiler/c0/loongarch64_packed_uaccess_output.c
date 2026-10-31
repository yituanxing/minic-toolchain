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

/* Linux 6.6.143 uses a deliberately wide struct only to make the GNU "m"
 * operand describe a faulting user address. Its type is not the ld/st width. */
#define LOONG_UACCESS_STORE(OPCODE) \
    "1: " OPCODE " %z2, %1  # __put_user_asm\n2:\n" LOONG_GPR_MAP \
    ".pushsection __ex_table, \"a\"\n" \
    ".balign 4\n" \
    ".long ((1b) - .)\n" \
    ".long ((2b) - .)\n" \
    ".short (2)\n" \
    ".short ((((.L__gpr_num_%0) << 0) | ((.L__gpr_num_zero) << 5)))\n" \
    ".popsection\n"

struct __large_struct { unsigned long buf[100]; };

#define LOONG_LARGE_LOAD(NAME, OPCODE, TYPE) \
    void NAME(int *err, TYPE *result, const TYPE *user) { \
        __asm__ __volatile__(LOONG_UACCESS(OPCODE) \
            : "+r"(*err), "=r"(*result) \
            : "m"(*(const struct __large_struct *)user)); \
    }

#define LOONG_LARGE_STORE(NAME, OPCODE, TYPE) \
    void NAME(int *err, TYPE *user, TYPE value) { \
        __asm__ __volatile__(LOONG_UACCESS_STORE(OPCODE) \
            : "+r"(*err), "=m"(*(struct __large_struct *)user) \
            : "Jr"(value)); \
    }

LOONG_LARGE_LOAD(linux_large_get_byte, "ld.b", unsigned char)
LOONG_LARGE_LOAD(linux_large_get_half, "ld.h", unsigned short)
LOONG_LARGE_LOAD(linux_large_get_word, "ld.w", unsigned int)
LOONG_LARGE_LOAD(linux_large_get_double, "ld.d", unsigned long)
LOONG_LARGE_STORE(linux_large_put_byte, "st.b", unsigned char)
LOONG_LARGE_STORE(linux_large_put_half, "st.h", unsigned short)
LOONG_LARGE_STORE(linux_large_put_word, "st.w", unsigned int)
LOONG_LARGE_STORE(linux_large_put_double, "st.d", unsigned long)

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
