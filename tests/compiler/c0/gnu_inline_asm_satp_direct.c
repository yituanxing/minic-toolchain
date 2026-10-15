static unsigned long satp_direct(unsigned long identity_satp) {
    unsigned long prior;

    __asm__ __volatile__("csrw satp, %0"
                         :
                         : "rK"(identity_satp)
                         : "memory");

    __asm__ __volatile__("csrrw %0, satp, %1"
                         : "=r"(prior)
                         : "rK"(0UL)
                         : "memory");
    return prior;
}

unsigned long satp_direct_probe(unsigned long identity_satp) {
    return satp_direct(identity_satp);
}
