/* Negative: the Fam10h MMCONFIG hardware ABI permits AL, never CL.
 * GNU can assemble this text, but it is not the Linux source-qualified
 * MMCONFIG form and MiniC must reject the unknown physical effect. */
unsigned char mmconfig_wrong_accumulator(void *address) {
    unsigned char value;
    __asm__ volatile("movb (%1),%%cl" : "=a"(value) : "r"(address));
    return value;
}
