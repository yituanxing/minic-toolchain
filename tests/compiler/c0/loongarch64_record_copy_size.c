/* Local LoongArch code-size regression: preserve unaligned byte semantics
 * without emitting a full unrolled instruction sequence for every byte. */
struct record32 {
    unsigned char bytes[32];
};

struct record32 minic_record32_by_value(struct record32 input) {
    struct record32 output = input;
    return output;
}

void minic_record32_copy(struct record32 *dst,
                         const struct record32 *src) {
    *dst = *src;
}

unsigned char minic_record32_load(const struct record32 *src) {
    struct record32 local = *src;
    return local.bytes[31];
}
