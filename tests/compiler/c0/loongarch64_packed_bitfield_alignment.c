/* Prove byte-aligned and nonzero byte-offset bit-field access through a
 * packed source address. LoongArch LD.W/LD.D must never target that address
 * unless alignment has been proved; GNU C permits packed bit-field layout. */
struct __attribute__((packed)) PackedBits {
    unsigned char pad;
    unsigned int lo : 5;
    unsigned int hi : 17;
    unsigned int tail : 10;
};

unsigned read_packed_high(const struct PackedBits *p) {
    return p->hi;
}

void write_packed_high(struct PackedBits *p, unsigned value) {
    p->hi = value;
}

unsigned read_packed_tail(const struct PackedBits *p) {
    return p->tail;
}

struct __attribute__((packed)) PackedScalar {
    unsigned char pad;
    unsigned int value;
    void *pointer;
};

unsigned read_packed_scalar(const struct PackedScalar *p) {
    return p->value;
}

void write_packed_scalar(struct PackedScalar *p, unsigned value) {
    p->value = value;
}

void *read_packed_pointer(const struct PackedScalar *p) {
    return p->pointer;
}

struct NaturalScalar {
    unsigned value;
    void *pointer;
};

unsigned read_natural_scalar(const struct NaturalScalar *p) {
    return p->value;
}

void *read_natural_pointer(const struct NaturalScalar *p) {
    return p->pointer;
}

struct __attribute__((packed)) PackedWide {
    unsigned char pad;
    unsigned __int128 wide;
};

unsigned __int128 read_packed_wide(const struct PackedWide *p) {
    return p->wide;
}

void write_packed_wide(struct PackedWide *p, unsigned __int128 value) {
    p->wide = value;
}

static struct { unsigned __int128 wide; } naturally_aligned_wide;

unsigned __int128 read_static_wide(void) {
    return naturally_aligned_wide.wide;
}
