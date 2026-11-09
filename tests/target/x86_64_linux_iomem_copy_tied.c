/* Original Linux arch/x86/lib/iomem.c GNU register contracts. In particular
 * the outputs are updated C lvalues and the inputs use numeric ties. */
typedef unsigned long usize;

__attribute__((noinline)) static int rep_movs(void *dest,
    const void *src, usize bytes) {
    unsigned long count, out_dest, out_src;
    __asm__ volatile("rep ; movsl\n\t"
        "testb $2,%b4\n\t"
        "je 1f\n\t"
        "movsw\n"
        "1:\ttestb $1,%b4\n\t"
        "je 2f\n\t"
        "movsb\n"
        "2:"
        : "=&c"(count), "=&D"(out_dest), "=&S"(out_src)
        : "0"(bytes / 4U), "q"(bytes),
          "1"((long)dest), "2"((long)src)
        : "memory");
    return count == 0UL && out_dest == (unsigned long)dest + bytes &&
        out_src == (unsigned long)src + bytes;
}

__attribute__((noinline)) static int copy_one_byte(
    unsigned char *dest, const unsigned char *src) {
    unsigned char *end_dest = dest;
    const unsigned char *end_src = src;
    __asm__ volatile("movsb" : "=&D"(end_dest), "=&S"(end_src)
        : "0"(end_dest), "1"(end_src) : "memory");
    return end_dest == dest + 1 && end_src == src + 1;
}

__attribute__((noinline)) static int copy_one_word(
    unsigned char *dest, const unsigned char *src) {
    unsigned char *end_dest = dest;
    const unsigned char *end_src = src;
    __asm__ volatile("movsw" : "=&D"(end_dest), "=&S"(end_src)
        : "0"(end_dest), "1"(end_src) : "memory");
    return end_dest == dest + 2 && end_src == src + 2;
}

int main(void) {
    unsigned char src[32], dest[32];
    unsigned long n, i;
    for (i = 0UL; i < 32UL; ++i) src[i] = (unsigned char)(i * 7UL + 3UL);
    for (n = 0UL; n <= 31UL; ++n) {
        for (i = 0UL; i < 32UL; ++i) dest[i] = 0x5aU;
        if (!rep_movs(dest, src, n)) return 1;
        for (i = 0UL; i < n; ++i)
            if (dest[i] != src[i]) return 2;
        for (; i < 32UL; ++i)
            if (dest[i] != 0x5aU) return 3;
    }
    dest[0] = dest[1] = 0;
    if (!copy_one_byte(dest, src) || dest[0] != src[0]) return 4;
    if (!copy_one_word(dest, src) ||
        dest[0] != src[0] || dest[1] != src[1]) return 5;
    return 0;
}
