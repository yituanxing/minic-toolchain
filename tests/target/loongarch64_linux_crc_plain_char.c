/* GNU LoongArch CRC instruction ABI. Plain char is SIGNED on LP64D.
 * Synthetic builtin signatures must use the selected target char model. */
int minic_test_loongarch_crc_char(char data, int seed) {
    return __builtin_loongarch_crc_w_b_w(data, seed);
}

int minic_test_loongarch_crcc_char(char data, int seed) {
    return __builtin_loongarch_crcc_w_b_w(data, seed);
}
