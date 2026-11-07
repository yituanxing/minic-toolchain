void not_an_isa_contract(unsigned long value) {
    __asm__ volatile ("amswap_db.w %0, %0, %0" : : "r" (value) : "memory");
}
