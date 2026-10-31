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

/* The leaf call is a single shared source-AST expression. Both specialized
 * middle callers must get a local leaf clone even when the leaf's literal
 * argument does not acquire new transitive integer facts. */
static __attribute__((always_inline)) inline unsigned long
probe_nested_leaf(unsigned long input) {
    return (input * 5UL) + 2UL;
}

static __attribute__((always_inline)) inline unsigned long
probe_nested_middle(unsigned long input) {
    return probe_nested_leaf(11UL) + input;
}

__attribute__((section(".head.text")))
unsigned long probe_nested_early(void) {
    return probe_nested_middle(3UL);
}

__attribute__((section(".init.text")))
unsigned long probe_nested_later(void) {
    return probe_nested_middle(3UL);
}
