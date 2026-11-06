/* Privileged task selector and vDSO getcpu alternatives: never executed. */
void load_task_selector(unsigned short selector) {
    __asm__ __volatile__("ltr %w0" : : "q"(selector));
}
unsigned short store_task_selector16(void) {
    unsigned short value;
    __asm__ __volatile__("str %0" : "=r"(value));
    return value;
}
unsigned int store_task_selector32(void) {
    unsigned int value;
    __asm__ __volatile__("str %0" : "=r"(value));
    return value;
}
unsigned long store_task_selector64(void) {
    unsigned long value;
    __asm__ __volatile__("str %0" : "=r"(value));
    return value;
}
unsigned int vdso_getcpu_lsl(void) {
    unsigned int result;
    __asm__ __volatile__(
        "# ALT: oldnstr\n"
        "661:\n\tlsl %[seg],%[p]\n"
        "662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        ".long 661b - .\n"
        ".long 6641f - .\n"
        ".4byte (16*32+22)\n"
        ".byte 663b - 661b\n"
        ".byte 6651f - 6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement,\"ax\"\n"
        "# ALT: replacement1\n"
        "6641:\n\t.byte 0xf3,0x0f,0xc7,0xf8\n"
        "6651:\n"
        ".popsection\n"
        : [p] "=a"(result) : "i"(0), [seg] "r"(123));
    return result;
}
