#include <stdbool.h>

/* Exact Linux 6.6 GNU .irp register map; no substitute C implementation. */
#define GPR_TABLE \
    "\t.irp num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
    "\t.equ .L__gpr_num_x\\num, \\num\n" \
    "\t.equ .L__gpr_num_w\\num, \\num\n" \
    "\t.endr\n" \
    "\t.equ .L__gpr_num_xzr, 31\n" \
    "\t.equ .L__gpr_num_wzr, 31\n"

#define RNG_BODY(SELECTOR) \
    GPR_TABLE \
    "\t.macro mrs_s, rt, sreg\n" \
    ".inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n\t.endm\n" \
    "\tmrs_s %0, (((3)<<19)|((3)<<16)|((2)<<12)|((4)<<8)|((" SELECTOR ")<<5))\n" \
    "\t.purgem mrs_s\n\n" \
    "\tcset %w1, ne\n"

bool linux_rndr(unsigned long *output) {
    bool valid;
    asm volatile(RNG_BODY("0") : "=r"(*output), "=r"(valid) : : "cc");
    return valid;
}

bool linux_rndrrs(unsigned long *output) {
    bool valid;
    asm volatile(RNG_BODY("1") : "=r"(*output), "=r"(valid) : : "cc");
    return valid;
}

#define USER_CACHE_TAIL \
    GPR_TABLE \
    ".pushsection __ex_table, \"a\"\n" \
    ".align 2\n" \
    ".long ((1b) - .)\n" \
    ".long ((2b) - .)\n" \
    ".short (2)\n" \
    ".short ((((.L__gpr_num_%w0)<<0)|((.L__gpr_num_wzr)<<5)))\n" \
    ".popsection\n"

#define CACHE_FUNCTION(NAME, INSN) \
    int NAME(unsigned long address) { \
        int status; \
        asm volatile("1: " INSN ", %1\n\tmov %w0, #0\n2:\n" \
            USER_CACHE_TAIL : "=r"(status) : "r"(address)); \
        return status; \
    }

CACHE_FUNCTION(linux_dc_civac, "dc civac")
CACHE_FUNCTION(linux_sys_c13, "sys 3, c7, c13, 1")
CACHE_FUNCTION(linux_sys_c12, "sys 3, c7, c12, 1")
CACHE_FUNCTION(linux_ic_ivau, "ic ivau")
