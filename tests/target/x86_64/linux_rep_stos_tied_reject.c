/* Negative: replacing GNU tied destination constraint "1" with generic
 * "r" cannot prove that RDI points to the caller's destination. */
typedef unsigned int u32;
void *linux_memset32_untied(u32 *dest, u32 data, unsigned long count) {
    long d0, d1;
    __asm__ volatile("rep\n\tstosl" : "=&c"(d0), "=&D"(d1)
        : "a"(data), "r"(dest), "0"(count) : "memory");
    return dest;
}
