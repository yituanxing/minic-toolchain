/* GNU-preprocessed model of Linux bitfield.h's helper chain.  No Linux symbol
 * is special to the compiler: the guard and diagnostic are ordinary GNU C. */
extern void invalid_mask(void) __attribute__((error("invalid mask")));
extern void value_does_not_fit(void) __attribute__((error("value does not fit")));

static inline __attribute__((always_inline))
unsigned long multiplier(unsigned long field) {
    if ((field | (field - 1UL)) & ((field | (field - 1UL)) + 1UL))
        invalid_mask();
    return field & -field;
}

static inline __attribute__((always_inline))
unsigned long mask(unsigned long field) {
    return field / multiplier(field);
}

static inline __attribute__((always_inline))
unsigned long encode(unsigned long value, unsigned long field) {
    if (__builtin_constant_p(value) && (value & ~mask(field)))
        value_does_not_fit();
    return (value & mask(field)) * multiplier(field);
}

static unsigned long dynamic_value(unsigned long value) {
    return encode(value, 0x1cUL);
}

static volatile unsigned long side_effect_count;

static inline __attribute__((always_inline))
unsigned long observed_branch(unsigned long value) {
    if (value == 1UL)
        ++side_effect_count;
    return value + 1UL;
}

int main(int argc, char **argv) {
    (void)argv;
    return (encode(3UL, 0x1cUL) != 12UL) ||
           (dynamic_value((unsigned long)argc + 1UL) != 8UL) ||
           (observed_branch(1UL) != 2UL) ||
           (side_effect_count != 1UL);
}
