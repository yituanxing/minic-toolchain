/* Linux signal-frame FPU save/restore has two SMAP alternatives and a
 * type-15 uaccess fault record; this is not an unguarded FXSAVE. */
struct linux_fregs { unsigned char bytes[108]; };
struct linux_fxregs { unsigned char bytes[512]; };

#define SMAP_PATCH(replacement) \
    "# ALT: oldnstr\n" "661:\n\t" "" "\n662:\n" \
    "# ALT: padding\n" \
    ".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")) > 0) * " \
    "((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")),0x90\n" \
    "663" ":\n" \
    ".pushsection .altinstructions,\"a\"\n" \
    " .long 661b - .\n" \
    " .long " "664""1""f - .\n" \
    " .4byte " "( 9*32+20)" "\n" \
    " .byte " "663""b-661b" "\n" \
    " .byte " "665""1""f-""664""1""f" "\n" \
    ".popsection\n" \
    ".pushsection .altinstr_replacement, \"ax\"\n" \
    "# ALT: replacement " "1" "\n" \
    "664""1"":\n\t" replacement "\n" "665""1" ":\n" \
    ".popsection\n"

#define FPU_USER_FRAME(opcode) \
    SMAP_PATCH(".byte 0x0f,0x01,0xcb") \
    "\n1: " opcode "\n2: " \
    SMAP_PATCH(".byte 0x0f,0x01,0xca") \
    "\n" " .pushsection \"__ex_table\",\"a\"\n" \
    " .balign 4\n" " .long (" "1b" ") - .\n" \
    " .long (" "2b" ") - .\n" " .long " "15" " \n" \
    " .popsection\n"

int linux_fnsave_user(struct linux_fregs *p) {
    int err;
    asm volatile(FPU_USER_FRAME("fnsave %[fx]; fwait")
        : [err] "=a"(err), [fx] "=m"(*p) : "0"(0), "m"(*p));
    return err;
}
int linux_fxsave_user(struct linux_fxregs *p) {
    int err;
    asm volatile(FPU_USER_FRAME("fxsaveq %[fx]")
        : [err] "=a"(err), [fx] "=m"(*p) : "0"(0), "m"(*p));
    return err;
}
int linux_fxrstor_user(struct linux_fxregs *p) {
    int err;
    asm volatile(FPU_USER_FRAME("fxrstorq %[fx]")
        : [err] "=a"(err), [fx] "=m"(*p) : "0"(0), "m"(*p));
    return err;
}

/* A named *input* memory operand is GNU ordinal %3, not %1 (the output
 * memory operand). The real Linux fpu/signal.i uses this variant. */
int linux_fxrstor_named_input(struct linux_fxregs *p) {
    int err;
    asm volatile(FPU_USER_FRAME("fxrstorq %[fx]")
        : [err] "=a"(err), "=m"(*p)
        : "0"(0), [fx] "m"(*p));
    return err;
}
