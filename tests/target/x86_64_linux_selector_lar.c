/* Linux force_valid_ss, including the actual ZF-dependent invalid selector
 * result. LAR reads access-right metadata rather than selector bits. */
typedef unsigned short u16;
typedef unsigned int u32;

u32 linux_read_ss_access_rights(u16 selector) {
    u32 access_rights;
    asm volatile("lar %[old_ss], %[ar]\n\t"
                 "jz 1f\n\t"
                 "xorl %[ar], %[ar]\n\t"
                 "1:"
        : [ar] "=r"(access_rights)
        : [old_ss] "rm"(selector));
    return access_rights;
}
