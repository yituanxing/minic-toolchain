/* Linux arm64 __get_kernel_nofault/__put_kernel_nofault: normal LDR/STR
 * with type-3 exception-table fixup, never LDTR/STTR or a plain C access.
 * This is compile-only kernel-mode code. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define GPR_NUMBER_TABLE \
    " .irp num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
    " .equ .L__gpr_num_x\\num, \\num\n" \
    " .equ .L__gpr_num_w\\num, \\num\n" \
    " .endr\n" \
    " .equ .L__gpr_num_xzr, 31\n" \
    " .equ .L__gpr_num_wzr, 31\n"

#define KERNEL_LOAD_TEMPLATE(mnemonic, reg) \
    "1: " mnemonic " " reg "1, [%2]\n" \
    "2:\n" GPR_NUMBER_TABLE \
    ".pushsection __ex_table, \"a\"\n" \
    ".align 2\n" \
    ".long ((1b) - .)\n" \
    ".long ((2b) - .)\n" \
    ".short (3)\n" \
    ".short ((((.L__gpr_num_%w0) << 0) | ((.L__gpr_num_%w1) << 5)))\n" \
    ".popsection\n"

#define KERNEL_STORE_TEMPLATE(mnemonic, reg) \
    "1: " mnemonic " " reg "1, [%2]\n" \
    "2:\n" GPR_NUMBER_TABLE \
    ".pushsection __ex_table, \"a\"\n" \
    ".align 2\n" \
    ".long ((1b) - .)\n" \
    ".long ((2b) - .)\n" \
    ".short (3)\n" \
    ".short ((((.L__gpr_num_%w0) << 0) | ((.L__gpr_num_wzr) << 5)))\n" \
    ".popsection\n"

#define DEFINE_KERNEL_READ(name, op, reg) \
    u64 name(const void *address) { \
        int err = 0; u64 data; \
        asm volatile(KERNEL_LOAD_TEMPLATE(op, reg) \
            : "+r" (err), "=r" (data) : "r" (address)); \
        return data + (u64)err; \
    }
#define DEFINE_KERNEL_WRITE(name, op, reg) \
    int name(void *address, u64 data) { \
        int err = 0; \
        asm volatile(KERNEL_STORE_TEMPLATE(op, reg) \
            : "+r" (err) : "rZ" (data), "r" (address)); \
        return err; \
    }

DEFINE_KERNEL_READ(kernel_nofault_read8,  "ldrb", "%w")
DEFINE_KERNEL_READ(kernel_nofault_read16, "ldrh", "%w")
DEFINE_KERNEL_READ(kernel_nofault_read32, "ldr",  "%w")
DEFINE_KERNEL_READ(kernel_nofault_read64, "ldr",  "%x")
DEFINE_KERNEL_WRITE(kernel_nofault_write8,  "strb", "%w")
DEFINE_KERNEL_WRITE(kernel_nofault_write16, "strh", "%w")
DEFINE_KERNEL_WRITE(kernel_nofault_write32, "str",  "%w")
DEFINE_KERNEL_WRITE(kernel_nofault_write64, "str",  "%x")
