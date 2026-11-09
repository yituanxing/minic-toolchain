/* Linux FXRSTORQ with type-17 register-specific -EFAULT exception table.
 * Only object/relocation inspection: the kernel handler is not in userspace. */
struct linux_fxregs_state { unsigned char bytes[512]; };

int linux_fxrstor_safe(struct linux_fxregs_state *p) {
    int error;
    asm volatile(
        "1:" "fxrstorq %[fx]" "\n\t" "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (" "1b" ") - .\n"
        " .long (" "2b" ") - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n"
        "extable_type_reg reg=" "%[err]" ", type="
            "(17 | ((-14) << 16))" " \n"
        ".purgem extable_type_reg\n"
        " .popsection\n"
        : [err] "=r"(error), "=m"(*p)
        : "0"(0), [fx] "m"(*p));
    return error;
}
