/* Linux 6.6 x86 boot string family: one exact GNU ABI for each instruction.
 * These instructions are safe in userspace and are verified at runtime. */
static int boot_memcmp(const void *left, const void *right,
    unsigned long count) {
    _Bool differs;
    asm("repe; cmpsb" "\n\t/* output condition code " "nz" "*/\n"
        : "=@cc" "nz" (differs), "+D" (left), "+S" (right), "+c" (count));
    return differs;
}

static unsigned int boot_divl(unsigned int lo, unsigned int hi,
    unsigned int divisor, unsigned int *remainder) {
    asm("divl %2" : "=a" (lo), "=d" (*remainder)
        : "rm" (divisor), "0" (lo), "1" (hi));
    return lo;
}

static void *boot_memcpy(void *dest, const void *source, unsigned long length) {
    long count, out_dest, out_source;
    asm volatile("rep ; movsq\n\t"
        "movq %4,%%rcx\n\t"
        "rep ; movsb\n\t"
        : "=&c" (count), "=&D" (out_dest), "=&S" (out_source)
        : "0" (length >> 3), "g" (length & 7), "1" (dest), "2" (source)
        : "memory");
    return dest;
}

int main(void) {
    unsigned char a[24], b[24];
    unsigned int rem = 99U;
    unsigned int quotient;
    unsigned int i;
    for (i = 0U; i < 24U; ++i) {
        a[i] = (unsigned char)(i * 7U + 3U);
        b[i] = 0U;
    }
    if (boot_memcmp(a,a,24U) != 0 || boot_memcmp(a,b,24U) == 0)
        return 1;
    quotient = boot_divl(0x00000005U,0x00000001U,3U,&rem);
    if (quotient != 0x55555557U || rem != 0U) return 2;
    if (boot_memcpy(b,a,21U) != b ||
        boot_memcmp(a,b,21U) != 0 || b[21] != 0U)
        return 3;
    return 0;
}
