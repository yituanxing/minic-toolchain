/* Linux 6.6.143 arch/x86/include/asm/kvm_para.h: the actual five-argument
 * alternative site and the one-through-four argument ABI siblings. This is
 * a privileged-instruction compile/ELF test, never executed in user mode. */
#define KVM_ALT_VMCALL                                                  \
    "# ALT: oldnstr\n"                                                 \
    "661:\n\t" "vmcall" "\n662:\n"                                    \
    "# ALT: padding\n"                                                 \
    ".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b"          \
    ")) > 0) * " "((" "665""1""f-""664""1""f" ")-(" "662b-661b"    \
    ")),0x90\n"                                                        \
    "663" ":\n"                                                        \
    ".pushsection .altinstructions,\"a\"\n"                         \
    " .long 661b - .\n"                                                \
    " .long " "664""1""f - .\n"                                       \
    " .4byte " "( 8*32+15)" "\n"                                      \
    " .byte " "663""b-661b" "\n"                                      \
    " .byte " "665""1""f-""664""1""f" "\n"                           \
    ".popsection\n"                                                    \
    ".pushsection .altinstr_replacement, \"ax\"\n"                 \
    "# ALT: replacement " "1" "\n"                                    \
    "664""1"":\n\t" "vmmcall" "\n"                                 \
    "665""1" ":\n"                                                   \
    ".popsection\n"

long linux_kvm_alt0(unsigned int nr) {
    long ret;
    asm volatile(KVM_ALT_VMCALL : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}
long linux_kvm_alt1(unsigned int nr, unsigned long p1) {
    long ret;
    asm volatile(KVM_ALT_VMCALL : "=a"(ret) : "a"(nr), "b"(p1) : "memory");
    return ret;
}
long linux_kvm_alt2(unsigned int nr, unsigned long p1, unsigned long p2) {
    long ret;
    asm volatile(KVM_ALT_VMCALL : "=a"(ret) : "a"(nr), "b"(p1), "c"(p2) : "memory");
    return ret;
}
long linux_kvm_alt3(unsigned int nr, unsigned long p1, unsigned long p2,
                    unsigned long p3) {
    long ret;
    asm volatile(KVM_ALT_VMCALL : "=a"(ret) : "a"(nr), "b"(p1), "c"(p2),
                 "d"(p3) : "memory");
    return ret;
}
long linux_kvm_alt4(unsigned int nr, unsigned long p1, unsigned long p2,
                    unsigned long p3, unsigned long p4) {
    long ret;
    asm volatile(KVM_ALT_VMCALL : "=a"(ret) : "a"(nr), "b"(p1), "c"(p2),
                 "d"(p3), "S"(p4) : "memory");
    return ret;
}
long linux_kvm_sev_direct0(unsigned int nr) {
    long ret;
    asm volatile("vmmcall" : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}
long linux_kvm_sev_direct3(unsigned int nr, unsigned long p1,
                           unsigned long p2, unsigned long p3) {
    long ret;
    asm volatile("vmmcall" : "=a"(ret) : "a"(nr), "b"(p1), "c"(p2),
                 "d"(p3) : "memory");
    return ret;
}
