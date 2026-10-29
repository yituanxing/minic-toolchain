/* Run ONLY the valid-pointer path. Fault recovery requires a kernel
 * __ex_table handler and is verified structurally in the ELF object. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
extern int linux_nofault_get_b(const void *, u8 *);
extern int linux_nofault_get_w(const void *, u16 *);
extern int linux_nofault_get_l(const void *, u32 *);
extern int linux_nofault_get_q(const void *, u64 *);
extern int linux_nofault_put_b(void *, u8);
extern int linux_nofault_put_w(void *, u16);
extern int linux_nofault_put_l(void *, u32);
extern int linux_nofault_put_q(void *, u64);

int main(void) {
    u64 memory = 0x1122334455667788ULL;
    u64 q;
    u32 l;
    u16 w;
    u8 b;
    if (linux_nofault_get_b(&memory, &b) || b != 0x88U) return 1;
    if (linux_nofault_get_w(&memory, &w) || w != 0x7788U) return 2;
    if (linux_nofault_get_l(&memory, &l) || l != 0x55667788U) return 3;
    if (linux_nofault_get_q(&memory, &q) || q != memory) return 4;
    if (linux_nofault_put_b(&memory, 0x99U) ||
        memory != 0x1122334455667799ULL) return 5;
    if (linux_nofault_put_w(&memory, 0xabbaU) ||
        memory != 0x112233445566abbaULL) return 6;
    if (linux_nofault_put_l(&memory, 0xd00dcafeU) ||
        memory != 0x11223344d00dcafeULL) return 7;
    if (linux_nofault_put_q(&memory, 0x123456789abcdef0ULL) ||
        memory != 0x123456789abcdef0ULL) return 8;
    return 0;
}
