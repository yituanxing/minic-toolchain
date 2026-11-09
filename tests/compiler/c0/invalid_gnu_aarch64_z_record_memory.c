struct minic_z_word {
    unsigned int bits;
};

unsigned int minic_z_invalid_record_constraint(const struct minic_z_word *word) {
    unsigned int result;
    __asm__ __volatile__("ldar %w0, %1"
                         : "=r"(result)
                         : "Z"(*word)
                         : "memory");
    return result;
}
