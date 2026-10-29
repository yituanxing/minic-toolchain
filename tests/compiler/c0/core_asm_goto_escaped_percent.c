/* An already-resolved GNU i operand and a literal percent escape share one
 * template. Only %0 is replaced by Core; %%0 must reach GAS as literal %0. */
int core_asm_goto_escaped_percent(void) {
    __asm__ goto(
        "j %l[yes]\n\t"
        ".ascii \"%%0\"\n\t"
        ".word %0\n\t"
        : : "i"(33) : : yes);
    return 0;
yes:
    return 1;
}
