/* GCC permits a packed 32-bit bit-field to begin after another bit-field.
 * Its storage window spans five bytes even though the value width is 32 bits. */
struct __attribute__((packed)) CrossWordBits {
    unsigned int first : 1;
    unsigned int value : 32;
};

struct __attribute__((packed)) CrossWordSignedBits {
    unsigned int first : 1;
    signed int value : 32;
};

_Static_assert(sizeof(struct CrossWordBits) == 5U, "packed 33 bits");
_Static_assert(sizeof(struct CrossWordSignedBits) == 5U, "signed packed 33 bits");

int signed_crossword(struct CrossWordSignedBits *p) {
    p->value = -17;
    if (p->value++ != -17) return 99;
    return p->value += 7;
}

unsigned read_crossword(const struct CrossWordBits *p) {
    return p->value;
}

void write_crossword(struct CrossWordBits *p, unsigned v) {
    p->value = v;
}

unsigned compound_crossword(struct CrossWordBits *p) {
    return p->value += 3U;
}

unsigned update_crossword(struct CrossWordBits *p) {
    return p->value++;
}

typedef unsigned long size_t;
extern void *mmap(void *, size_t, int, int, int, long);
extern int mprotect(void *, size_t, int);
extern int munmap(void *, size_t);

int main(void) {
    unsigned char *base = (unsigned char *)mmap((void *)0, 8192UL, 3, 34, -1, 0L);
    struct CrossWordBits *p;
    struct CrossWordSignedBits *signed_p;
    unsigned i;

    if (base == (void *)-1) return 1;
    p = (struct CrossWordBits *)(base + 4096UL - sizeof(*p));
    for (i = 0U; i < sizeof(*p); ++i) ((unsigned char *)p)[i] = 0U;
    if (mprotect(base + 4096UL, 4096UL, 0) != 0) return 2;
    p->first = 1U;
    write_crossword(p, 0x87654321U);
    if (p->first != 1U || read_crossword(p) != 0x87654321U) return 3;
    if (compound_crossword(p) != 0x87654324U) return 4;
    if (update_crossword(p) != 0x87654324U) return 5;
    if (p->first != 1U || read_crossword(p) != 0x87654325U) return 6;
    signed_p = (struct CrossWordSignedBits *)p;
    signed_p->first = 1U;
    if (signed_crossword(signed_p) != -9 ||
        signed_p->value != -9 || signed_p->first != 1U) return 8;
    if (munmap(base, 8192UL) != 0) return 7;
    return 0;
}
