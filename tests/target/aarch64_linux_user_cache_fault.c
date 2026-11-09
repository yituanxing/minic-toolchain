/* Linux 6.6 traps.c cache-maintenance trap emulation: every operation is
 * coupled to type-2 exception recovery that writes the C error register. */
#define USER_CACHE_OP(instruction) \
    "1:\t" instruction ", %1\n" \
    "\tmov\t%w0, #0\n" \
    "2:\n" \
    "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
    "\t.equ\t.L__gpr_num_x\\num, \\num\n" \
    "\t.equ\t.L__gpr_num_w\\num, \\num\n" \
    "\t.endr\n" \
    "\t.equ\t.L__gpr_num_xzr, 31\n" \
    "\t.equ\t.L__gpr_num_wzr, 31\n" \
    ".pushsection\t__ex_table, \"a\"\n" \
    ".align\t\t2\n" \
    ".long\t\t((" "1b" ") - .)\n" \
    ".long\t\t((" "2b" ") - .)\n" \
    ".short\t\t(" "2" ")\n" \
    ".short\t\t(" "(" "((.L__gpr_num_" "%w0" ") << " "0" ")" \
          " | " "((.L__gpr_num_" "wzr" ") << " "5" ")" ")" ")\n" \
    ".popsection\n"

int linux_cache_dc_civac(unsigned long address) {
    int ret;
    asm volatile(USER_CACHE_OP("dc civac") : "=r"(ret) : "r"(address));
    return ret;
}
int linux_cache_sys_c13(unsigned long address) {
    int ret;
    asm volatile(USER_CACHE_OP("sys 3, c7, c13, 1") : "=r"(ret) : "r"(address));
    return ret;
}
int linux_cache_sys_c12(unsigned long address) {
    int ret;
    asm volatile(USER_CACHE_OP("sys 3, c7, c12, 1") : "=r"(ret) : "r"(address));
    return ret;
}
int linux_cache_ic_ivau(unsigned long address) {
    int ret;
    asm volatile(USER_CACHE_OP("ic ivau") : "=r"(ret) : "r"(address));
    return ret;
}
