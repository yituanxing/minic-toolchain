int extended_asm_marker(int value) {
    asm("nop" : : "r"(value));
    return value + 1;
}
