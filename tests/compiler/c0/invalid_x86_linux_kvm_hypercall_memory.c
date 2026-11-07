#include "x86_linux_kvm_hypercall_full_abi.c"

/* The direct AMD form is a compiler memory barrier in the Linux source.
 * An unrelated CC clobber cannot claim the identical memory effect. */
long linux_kvm_sev_missing_memory(unsigned int nr, unsigned long p1,
                                  unsigned long p2, unsigned long p3) {
    long ret;
    asm volatile("vmmcall" : "=a"(ret) : "a"(nr), "b"(p1), "c"(p2),
                 "d"(p3) : "cc");
    return ret;
}
