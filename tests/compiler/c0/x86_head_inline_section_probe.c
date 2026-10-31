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

/* Linux overflow.h: the _Bool call only gains its range-specialized clone
 * after the integer and symbolic passes. A sectioned integer clone of the
 * enclosing helper has no Boolean parameter facts, but still needs the
 * late-created Boolean callee duplicated into its own section. */
static inline _Bool probe_overflow_expected(_Bool overflow) {
    return __builtin_expect(!!overflow, 0);
}

static inline unsigned long probe_overflow_product(unsigned long n,
                                                   unsigned long size) {
    unsigned long bytes;
    if (__builtin_expect(!!probe_overflow_expected(
            __builtin_mul_overflow(n, size, &bytes)), 0))
        return ~0UL;
    return bytes;
}

static inline unsigned long probe_overflow_wrapper(unsigned long n) {
    return probe_overflow_product(n, 2UL);
}

__attribute__((section(".head.text")))
unsigned long probe_overflow_early(void) {
    return probe_overflow_wrapper(3UL);
}

__attribute__((section(".init.text")))
unsigned long probe_overflow_later(void) {
    return probe_overflow_wrapper(3UL);
}
