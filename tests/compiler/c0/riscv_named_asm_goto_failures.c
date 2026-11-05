/* GNU names are resolved at C ingress and sealed in Core; `%l[...]` may
 * reference only a C label, `%[...]` only an input operand. The three
 * deliberately forged forms must fail before any target text is emitted. */
#if NAMED_GOTO_FAILURE == 1
int rv32_named_goto_missing_input(void) {
    asm volatile goto(".word %[ghost]" : : [key] "i"(11) : : enabled);
    return 0;
enabled:
    return 1;
}
#elif NAMED_GOTO_FAILURE == 2
int rv32_named_goto_wrong_kind(void) {
    asm volatile goto(".long %l[key] - ." : : [key] "i"(11) : : enabled);
    return 0;
enabled:
    return 1;
}
#elif NAMED_GOTO_FAILURE == 3
int rv32_named_goto_colliding_names(void) {
    asm volatile goto(".word %[key]\n.long %l[key] - ."
        : : [key] "i"(11) : : key);
    return 0;
key:
    return 1;
}
#else
#error NAMED_GOTO_FAILURE must select a deliberately invalid form
#endif
