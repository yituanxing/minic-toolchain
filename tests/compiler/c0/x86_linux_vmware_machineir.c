/* Derived from Linux 6.6.143 arch/x86/kernel/cpu/vmware.c. Privileged
 * opcodes are compile/assemble-only: this fixture must never execute. */
typedef unsigned int u32;
extern u32 vmware_hypercall_mode;

u32 minic_vmware_hypercall_probe(u32 command) {
    u32 eax, ecx, edx, ebx;
    switch (vmware_hypercall_mode) {
    case 2U:
        __asm__("vmcall" : "=a"(eax), "=c"(ecx), "=d"(edx), "=b"(ebx)
            : "a"(0x564D5868U), "c"(command), "d"(0), "b"(~0U)
            : "memory");
        break;
    case 1U:
        __asm__("vmmcall" : "=a"(eax), "=c"(ecx), "=d"(edx), "=b"(ebx)
            : "a"(0x564D5868U), "c"(command), "d"(0), "b"(~0U)
            : "memory");
        break;
    default:
        __asm__("inl (%%dx), %%eax" : "=a"(eax), "=c"(ecx), "=d"(edx), "=b"(ebx)
            : "a"(0x564D5868U), "c"(command), "d"(0x5658), "b"(~0U)
            : "memory");
        break;
    }
    return eax ^ ecx ^ edx ^ ebx;
}

#define VMWARE_ALT_MAX "((6651f-6641f) ^ (((6651f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) < (6652f-6642f)))))"
u32 minic_vmware_stealclock_probe(u32 arg1, u32 arg2) {
    u32 result, info;
    __asm__ volatile (
        "# ALT: oldinstr2\n"
        "661:\n\tmovw $0x5658, %%dx; inl (%%dx), %%eax\n"
        "662:\n"
        "# ALT: padding2\n"
        ".skip -( (" VMWARE_ALT_MAX " - (662b-661b)) > 0) * ("
            VMWARE_ALT_MAX " - (662b-661b)), 0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte (8*32+18)\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        " .long 661b - .\n"
        " .long 6642f - .\n"
        " .4byte (8*32+19)\n"
        " .byte 663b-661b\n"
        " .byte 6652f-6642f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement,\"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\tvmcall\n"
        "6651:\n"
        "# ALT: replacement 2\n"
        "6642:\n\tvmmcall\n"
        "6652:\n"
        ".popsection\n"
        : "=a"(result), "=c"(info)
        : "a"(0x564D5868U), "b"(0), "c"(91), "d"(0),
          "S"(arg1), "D"(arg2)
        : "memory");
    return result ^ info;
}
