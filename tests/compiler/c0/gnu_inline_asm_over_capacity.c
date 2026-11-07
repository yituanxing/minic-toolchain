/* GNU accepts this 17-input asm form. MiniC's Core currently represents at
 * most 16 data operands, so each target must reject it before code emission. */
unsigned long gnu_inline_asm_over_capacity(unsigned long x) {
    __asm__ __volatile__(
        "nop"
        :
        : "r"(x), "r"(x), "r"(x), "r"(x), "r"(x), "r"(x), "r"(x), "r"(x),
          "r"(x), "r"(x), "r"(x), "r"(x), "r"(x), "r"(x), "r"(x), "r"(x),
          "r"(x)
        : "memory");
    return x;
}
