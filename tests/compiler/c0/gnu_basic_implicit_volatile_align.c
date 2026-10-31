/* GNU basic asm has no colon operands and is implicitly volatile. */
int basic_asm_align(int value) {
    __asm__(".p2align 6");
    return value + 1;
}
