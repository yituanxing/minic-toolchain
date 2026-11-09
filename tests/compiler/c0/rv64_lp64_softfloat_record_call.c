/* LP64 base integer psABI: even records with FP leaves travel through the
 * integer aggregate ABI. Both direct and SSA-pointer calls return records
 * in a0/a1, while LP64D must reject until FP-record flattening exists. */
typedef struct OneFloat { float f; } OneFloat;
typedef struct OneDouble { double d; } OneDouble;

__attribute__((noinline)) OneFloat echo_float(OneFloat value) {
    return value;
}
__attribute__((noinline)) OneDouble echo_double(OneDouble value) {
    return value;
}

OneFloat direct_float(OneFloat value) {
    return echo_float(value);
}
OneDouble direct_double(OneDouble value) {
    return echo_double(value);
}
OneFloat indirect_float(OneFloat value, OneFloat (*callee)(OneFloat)) {
    return callee(value);
}
OneDouble indirect_double(OneDouble value, OneDouble (*callee)(OneDouble)) {
    return callee(value);
}

int main(void) {
    OneFloat f = {3.25f};
    OneDouble d = {6.5};
    OneFloat x = direct_float(f);
    OneDouble y = direct_double(d);
    OneFloat z = indirect_float(x, echo_float);
    OneDouble w = indirect_double(y, echo_double);
    const unsigned char *expected_float = (const unsigned char *)&f;
    const unsigned char *expected_double = (const unsigned char *)&d;
    const unsigned char *direct_float_bytes = (const unsigned char *)&x;
    const unsigned char *direct_double_bytes = (const unsigned char *)&y;
    const unsigned char *indirect_float_bytes = (const unsigned char *)&z;
    const unsigned char *indirect_double_bytes = (const unsigned char *)&w;
    unsigned long i;
    /* Byte-wise validation uses only RV64I integer instructions, so strict
     * -march=rv64i need not synthesize unsupported FP comparison opcodes. */
    for (i = 0UL; i < sizeof(f); ++i)
        if (expected_float[i] != direct_float_bytes[i] ||
            expected_float[i] != indirect_float_bytes[i]) return 1;
    for (i = 0UL; i < sizeof(d); ++i)
        if (expected_double[i] != direct_double_bytes[i] ||
            expected_double[i] != indirect_double_bytes[i]) return 2;
    return 0;
}
