typedef struct {
    unsigned reg:24;
    unsigned bank_idx:4;
} Register;

unsigned invalid_const_bitfield_mutation(void) {
    const Register value = { .reg = 5U, .bank_idx = 2U };
    value.reg = 1U;
    return value.reg;
}
