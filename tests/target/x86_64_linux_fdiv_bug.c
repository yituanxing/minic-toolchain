/* SPDX-License-Identifier: GPL-2.0 */
/* Original arch/x86/kernel/fpu/bugs.c x87 probe, compiled but not run. */
static double x __attribute__((section(".init.data"))) = 4195835.0;
static double y __attribute__((section(".init.data"))) = 3145727.0;

int minic_linux_fpu_check_fdiv(void) {
    int fdiv_bug;
    __asm__("fninit\n\t"
            "fldl %1\n\t"
            "fdivl %2\n\t"
            "fmull %2\n\t"
            "fldl %1\n\t"
            "fsubp %%st,%%st(1)\n\t"
            "fistpl %0\n\t"
            "fwait\n\t"
            "fninit"
            : "=m" (fdiv_bug)
            : "m" (x), "m" (y));
    return fdiv_bug;
}
