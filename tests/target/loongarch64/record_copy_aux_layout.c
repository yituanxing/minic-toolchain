/* Typed LoongArch64 MachineIR: an outlined record copy must not move the
 * following dynamic-allocation / unsigned FP conversion machine CFG away
 * from its physical fallthrough blocks. Both functions are production
 * compiler fixtures, not snippets of target assembly. */
struct LoongFortyBytes {
    unsigned char bytes[40];
};

void loong_record_copy_before_alloc(struct LoongFortyBytes *destination,
    const struct LoongFortyBytes *source, unsigned long size) {
    *destination = *source;
    volatile unsigned char *temporary =
        (volatile unsigned char *)__builtin_alloca((size & 31UL) + 32UL);
    temporary[0] = destination->bytes[0];
    destination->bytes[1] = temporary[0];
}

unsigned long loong_record_copy_before_float_conversion(
    struct LoongFortyBytes *destination,
    const struct LoongFortyBytes *source, double value) {
    *destination = *source;
    return (unsigned long)value;
}
