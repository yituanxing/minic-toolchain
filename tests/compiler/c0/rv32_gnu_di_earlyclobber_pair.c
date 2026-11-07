/* GNU RISC-V RV32 `=&r` on a DI lvalue occupies two consecutive GPRs.
 * Linux's get_user dispatch contains this exact +r / =&r / m shape.
 * The source `lw` sets only the low word: upper output bits are not defined
 * by the source ASM and must never be invented by the backend. */
unsigned long long rv32_di_pair_load(const unsigned int *memory) {
    unsigned long error = 0;
    unsigned long long value;
    __asm__ __volatile__("lw %1, %2" : "+r" (error), "=&r" (value)
        : "m" (*memory) : "memory");
    return value;
}

/* The lvalue transport must also handle unaligned C destinations; the
 * generated source assembly is still the original literal instruction. */
struct __attribute__((packed)) rv32_pair_slot {
    unsigned char padding;
    unsigned long long value;
};

void rv32_di_pair_unaligned(struct rv32_pair_slot *slot,
    const unsigned int *memory) {
    unsigned long error = 0;
    __asm__ __volatile__("lw %1, %2" : "+r" (error),
        "=&r" (slot->value) : "m" (*memory) : "memory");
}
