static __attribute__((always_inline)) inline unsigned long probe_helper(unsigned long input) {
    return (input + 7UL) ^ (input << 3);
}

__attribute__((section(".head.text")))
unsigned long probe_early(void) {
    return probe_helper(17UL);
}

__attribute__((section(".init.text")))
unsigned long probe_later(void) {
    return probe_helper(17UL);
}

__attribute__((section(".head.text")))
unsigned long probe_early_again(void) {
    return probe_helper(17UL);
}

static __attribute__((section(".special.text"), always_inline)) inline
unsigned long probe_explicit(unsigned long input) {
    return input * 3UL;
}

__attribute__((section(".head.text")))
unsigned long probe_explicit_owner(void) {
    return probe_explicit(13UL);
}
