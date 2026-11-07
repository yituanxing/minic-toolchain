/* Linux 6.6 x86 process_64.i privileged FSGSBASE, LKGS and loadsegment
 * exception-table contracts. Assemble only; none are safe in user mode. */
typedef unsigned short u16;
typedef unsigned long u64;

u64 linux_rdfsbase(void) {
    u64 v;
    asm volatile("rdfsbase %0" : "=r" (v) : : "memory");
    return v;
}
u64 linux_rdgsbase(void) {
    u64 v;
    asm volatile("rdgsbase %0" : "=r" (v) : : "memory");
    return v;
}
void linux_wrfsbase(u64 v) {
    asm volatile("wrfsbase %0" : : "r" (v) : "memory");
}
void linux_wrgsbase(u64 v) {
    asm volatile("wrgsbase %0" : : "r" (v) : "memory");
}

/* The register-table GNU macro is intentionally included verbatim: Linux
 * exception metadata must stay part of the exact source contract. */
#define EXTABLE_REGISTER_TABLE \
    ".macro extable_type_reg type:req reg:req\n" \
    ".set .Lfound, 0\n" \
    ".set .Lregnr, 0\n" \
    ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n" \
    ".ifc \\reg, %%\\rs\n" \
    ".set .Lfound, .Lfound+1\n" \
    ".long \\type + (.Lregnr << 8)\n" \
    ".endif\n" \
    ".set .Lregnr, .Lregnr+1\n" \
    ".endr\n" \
    ".set .Lregnr, 0\n" \
    ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n" \
    ".ifc \\reg, %%\\rs\n" \
    ".set .Lfound, .Lfound+1\n" \
    ".long \\type + (.Lregnr << 8)\n" \
    ".endif\n" \
    ".set .Lregnr, .Lregnr+1\n" \
    ".endr\n" \
    ".if (.Lfound != 1)\n" \
    ".error \"extable_type_reg: bad register argument\"\n" \
    ".endif\n" \
    ".endm\n"

void linux_lkgs_with_register_fixup(u16 selector) {
    u16 sel = selector;
    asm __inline volatile("1: " " " ".byte 0xf2,0x0f,0x00,0xf7 ;" " "
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (1b) - .\n"
        EXTABLE_REGISTER_TABLE
        "extable_type_reg reg=" "%k[sel]" ", type=" "(17 | ((0) << 16))" " \n"
        ".purgem extable_type_reg\n"
        " .popsection\n" : [sel] "+D" (sel));
}

#define LOAD_SEGMENT_WITH_REG_FIXUP(seg, selector) \
    asm volatile("\t\t\t\t\t\t\n" \
        "1:\tmovl %k0,%%" seg "\t\t\n" \
        " .pushsection \"__ex_table\",\"a\"\n" \
        " .balign 4\n" \
        " .long (1b) - .\n" \
        " .long (1b) - .\n" \
        EXTABLE_REGISTER_TABLE \
        "extable_type_reg reg=" "%k0" ", type=" "(17 | ((0) << 16))" " \n" \
        ".purgem extable_type_reg\n" \
        " .popsection\n" : "+r" (selector) : : "memory")

void linux_load_es_register(u16 v) { LOAD_SEGMENT_WITH_REG_FIXUP("es", v); }
void linux_load_ds_register(u16 v) { LOAD_SEGMENT_WITH_REG_FIXUP("ds", v); }
void linux_load_ss_register(u16 v) { LOAD_SEGMENT_WITH_REG_FIXUP("ss", v); }
