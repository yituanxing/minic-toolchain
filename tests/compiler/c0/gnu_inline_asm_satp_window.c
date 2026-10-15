static unsigned long satp_transition(unsigned long identity_satp) {
    unsigned long prior;

    __asm__ __volatile__("csrw satp, %0"
                         :
                         : "rK"(identity_satp)
                         : "memory");

    prior = 0UL;
    __asm__ __volatile__("csrrw %0, satp, %1"
                         : "=r"(prior)
                         : "rK"(prior)
                         : "memory");
    return prior;
}

unsigned long satp_window_probe(unsigned long identity_satp) {
    return satp_transition(identity_satp);
}
