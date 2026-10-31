#include "x86_linux_kvm_hypercall_full_abi.c"

/* A fifth argument exists in the GNU constraints, but must be physical RSI.
 * RDI is a valid GCC constraint and must NOT silently satisfy this contract. */
long linux_kvm_wrong_fifth_register(unsigned int nr, unsigned long p1,
                                   unsigned long p2, unsigned long p3,
                                   unsigned long p4) {
    long ret;
    asm volatile(KVM_ALT_VMCALL : "=a"(ret) : "a"(nr), "b"(p1), "c"(p2),
                 "d"(p3), "D"(p4) : "memory");
    return ret;
}
