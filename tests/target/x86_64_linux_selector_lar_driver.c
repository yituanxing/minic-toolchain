typedef unsigned short u16;
typedef unsigned int u32;
extern u32 linux_read_ss_access_rights(u16);

static u32 gnu_read_ss_access_rights(u16 selector) {
    u32 access_rights;
    asm volatile("lar %1, %0\n\t"
                 "jz 1f\n\t"
                 "xorl %0, %0\n\t"
                 "1:"
        : "=r"(access_rights) : "rm"(selector));
    return access_rights;
}

int main(void) {
    u16 ss;
    asm volatile("movw %%ss, %0" : "=r"(ss));
    if (linux_read_ss_access_rights(0) != 0U) return 1;
    if (linux_read_ss_access_rights(1) != gnu_read_ss_access_rights(1))
        return 2;
    if (linux_read_ss_access_rights(ss) != gnu_read_ss_access_rights(ss))
        return 3;
    return 0;
}
