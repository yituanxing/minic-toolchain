/* Exact GNU Linux memset16/32/64 `rep; stos{w,l,q}` source ABI:
 * =&c / =&D outputs tied to initial destination/count via 1 / 0. */
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

void *linux_memset16(u16 *dest, u16 data, u64 count) {
    long d0, d1;
    __asm__ volatile("rep\n\tstosw" : "=&c"(d0), "=&D"(d1)
        : "a"(data), "1"(dest), "0"(count) : "memory");
    return dest;
}

void *linux_memset32(u32 *dest, u32 data, u64 count) {
    long d0, d1;
    __asm__ volatile("rep\n\tstosl" : "=&c"(d0), "=&D"(d1)
        : "a"(data), "1"(dest), "0"(count) : "memory");
    return dest;
}

void *linux_memset64(u64 *dest, u64 data, u64 count) {
    long d0, d1;
    __asm__ volatile("rep\n\tstosq" : "=&c"(d0), "=&D"(d1)
        : "a"(data), "1"(dest), "0"(count) : "memory");
    return dest;
}

int main(void) {
    u16 array16[3] = {0, 0, 0};
    u32 array32[3] = {0, 0, 0};
    u64 array64[3] = {0, 0, 0};
    linux_memset16(array16, 0x3355U, 3UL);
    linux_memset32(array32, 0x89abcdefU, 3UL);
    linux_memset64(array64, 0x0123456789abcdefUL, 3UL);
    if (array16[0] != 0x3355U || array16[1] != 0x3355U ||
        array16[2] != 0x3355U) return 1;
    if (array32[0] != 0x89abcdefU || array32[1] != 0x89abcdefU ||
        array32[2] != 0x89abcdefU) return 2;
    if (array64[0] != 0x0123456789abcdefUL ||
        array64[1] != 0x0123456789abcdefUL ||
        array64[2] != 0x0123456789abcdefUL) return 3;
    return 0;
}
