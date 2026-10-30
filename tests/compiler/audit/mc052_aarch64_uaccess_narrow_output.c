/* Linux 6.6 AArch64 uaccess: the exception-table fixup is attached to the
 * faulting LDTR.  Its register writeback is wider than a C byte/halfword
 * lvalue; only the output object's exact bytes may be stored afterward. */
#define UACCESS_FIXUP \
    ".irp num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
    ".equ .L__gpr_num_x\\num,\\num\n" \
    ".equ .L__gpr_num_w\\num,\\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_xzr,31\n" \
    ".equ .L__gpr_num_wzr,31\n" \
    ".pushsection __ex_table,\"a\"\n" \
    ".align 2\n" \
    ".long ((1b) - .)\n" \
    ".long ((2b) - .)\n" \
    ".short (2)\n" \
    ".short((((.L__gpr_num_%w0)<<0)|((.L__gpr_num_%w1)<<5)))\n" \
    ".popsection\n"

#define GET_USER_MNEMONIC(mnemonic, output, pointer) do { \
    int error = 0; \
    __asm__ __volatile__("1: " mnemonic "\n2:\n" UACCESS_FIXUP \
                         : "+r"(error), "=r"(output) : "r"(pointer)); \
} while (0)

/* Packed second field makes an 8-byte writeback visibly unsafe. */
struct __attribute__((packed)) NarrowPair {
    unsigned char guard;
    unsigned char value;
};

void read_u8(unsigned char *user, struct NarrowPair *pair) {
    GET_USER_MNEMONIC("ldtrb %w1, [%2]", pair->value, user);
}
void read_u16(unsigned short *user, unsigned short *result) {
    GET_USER_MNEMONIC("ldtrh %w1, [%2]", *result, user);
}
void read_u32(unsigned int *user, unsigned int *result) {
    GET_USER_MNEMONIC("ldtr %w1, [%2]", *result, user);
}
void read_u64(unsigned long *user, unsigned long *result) {
    GET_USER_MNEMONIC("ldtr %x1, [%2]", *result, user);
}
void read_pointer(void **user, void **result) {
    GET_USER_MNEMONIC("ldtr %x1, [%2]", *result, user);
}

int main(void) {
    unsigned char byte = 0xa5U;
    unsigned short half = 0x9a67U;
    unsigned int word = 0x9abcdef0U;
    unsigned long dword = 0x1234567887654321UL;
    void *pointer_source = &word;
    void *pointer_result = (void *)0;
    struct NarrowPair pair = {0x31U, 0U};
    unsigned short half_result = 0U;
    unsigned int word_result = 0U;
    unsigned long dword_result = 0U;
    read_u8(&byte, &pair);
    read_u16(&half, &half_result);
    read_u32(&word, &word_result);
    read_u64(&dword, &dword_result);
    read_pointer(&pointer_source, &pointer_result);
    return (pair.guard == 0x31U && pair.value == byte &&
            half_result == half && word_result == word &&
            dword_result == dword && pointer_result == pointer_source) ? 0 : 1;
}

#ifdef MC052_OVERSIZE_NEGATIVE
/* A 16-byte C output cannot be represented by one AArch64 GPR. */
void reject_u128(unsigned long *user, unsigned __int128 *result) {
    GET_USER_MNEMONIC("ldtr %x1, [%2]", *result, user);
}
#endif
