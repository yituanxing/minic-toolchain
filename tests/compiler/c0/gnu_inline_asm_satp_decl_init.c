#define satp_swap_like(value)                                                \
({                                                                           \
    unsigned long __v = (unsigned long)(value);                              \
    __asm__ __volatile__("csrrw %0, satp, %1"                                \
                         : "=r"(__v)                                         \
                         : "rK"(__v)                                         \
                         : "memory");                                        \
    __v;                                                                     \
})

static unsigned long satp_decl_transition(unsigned long identity_satp) {
    unsigned long old_satp;

    __asm__ __volatile__("csrw satp, %0"
                         :
                         : "rK"(identity_satp)
                         : "memory");

    old_satp = satp_swap_like(0ULL);
    return old_satp;
}

unsigned long satp_decl_window_probe(unsigned long identity_satp) {
    return satp_decl_transition(identity_satp);
}
