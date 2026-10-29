/* GNU/MiniC native oracle for packed bit-fields at an allocation boundary.
 * The record ends at the first page's last byte; a 32-bit load of `tail`
 * would read two bytes of the PROT_NONE second page. */
typedef unsigned long size_t;
extern void *mmap(void *, size_t, int, int, int, long);
extern int mprotect(void *, size_t, int);
extern int munmap(void *, size_t);

struct __attribute__((packed)) PackedBits {
    unsigned char pad;
    unsigned int lo : 5;
    unsigned int hi : 17;
    unsigned int tail : 10;
};

/* A full four-byte packed record is still allowed to start at an odd
 * address; byte_offset=0 and in-record bounds alone cannot justify ld.w. */
struct __attribute__((packed)) PackedBase {
    unsigned int low : 3;
    unsigned int unused : 29;
};

struct __attribute__((packed)) PackedSignedTail {
    unsigned char prefix[3];
    signed int value : 9;
};

__attribute__((noinline)) int read_signed_tail(const struct PackedSignedTail *p) {
    return p->value;
}

__attribute__((noinline)) int modify_signed_tail(struct PackedSignedTail *p) {
    p->value = -13;
    if (p->value++ != -13) return 99;
    return p->value += 4;
}

__attribute__((noinline)) unsigned read_base(const struct PackedBase *p) {
    return p->low;
}

__attribute__((noinline)) void write_base(struct PackedBase *p, unsigned value) {
    p->low = value;
}

__attribute__((noinline)) unsigned read_tail(const struct PackedBits *p) {
    return p->tail;
}

__attribute__((noinline)) unsigned read_high(const struct PackedBits *p) {
    return p->hi;
}

__attribute__((noinline)) void write_tail(struct PackedBits *p, unsigned value) {
    p->tail = value;
}

__attribute__((noinline)) void write_high(struct PackedBits *p, unsigned value) {
    p->hi = value;
}

__attribute__((noinline)) unsigned update_tail(struct PackedBits *p) {
    return p->tail++;
}

__attribute__((noinline)) unsigned compound_tail(struct PackedBits *p) {
    return p->tail += 3U;
}

int main(void) {
    unsigned char *allocation;
    struct PackedBits *p;
    struct PackedBase *unaligned;
    struct PackedSignedTail *signed_tail;
    unsigned i;

    if (sizeof(struct PackedBits) != 5U) return 1;
    allocation = (unsigned char *)mmap((void *)0, 8192UL, 3, 34, -1, 0L);
    if (allocation == (void *)-1) return 2;
    p = (struct PackedBits *)(allocation + 4096UL - sizeof(*p));
    for (i = 0U; i != sizeof(*p); ++i)
        ((unsigned char *)p)[i] = 0U;
    if (mprotect(allocation + 4096UL, 4096UL, 0) != 0) return 3;

    write_high(p, 0x15555U);
    if (read_high(p) != 0x15555U) return 4;
    write_tail(p, 511U);
    if (read_tail(p) != 511U) return 5;
    if (update_tail(p) != 511U || read_tail(p) != 512U) return 6;
    if (compound_tail(p) != 515U || read_tail(p) != 515U) return 7;
    if (read_high(p) != 0x15555U) return 8;
    unaligned = (struct PackedBase *)(allocation + 4096UL - 5UL);
    if (sizeof(*unaligned) != 4U) return 10;
    write_base(unaligned, 5U);
    if (read_base(unaligned) != 5U) return 11;
    signed_tail = (struct PackedSignedTail *)(allocation + 4096UL - 5UL);
    if (sizeof(*signed_tail) != 5U) return 12;
    for (i = 0U; i != sizeof(*signed_tail); ++i)
        ((unsigned char *)signed_tail)[i] = 0U;
    if (modify_signed_tail(signed_tail) != -8 ||
        read_signed_tail(signed_tail) != -8) return 13;
    if (munmap(allocation, 8192UL) != 0) return 9;
    return 0;
}
