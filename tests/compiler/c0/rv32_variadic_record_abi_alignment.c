/* RV32 psABI: an unnamed two-word argument requires an even-numbered GPR
 * pair only if its ABI alignment is 8 bytes. Size 8 is not sufficient. */
typedef struct { unsigned int low, high; } Pair32;
typedef struct __attribute__((packed)) {
    unsigned int low, high;
} PackedPair32;
typedef struct {
    unsigned int low, high;
} __attribute__((aligned(8))) AlignedPair32;

_Static_assert(sizeof(Pair32) == 8 && _Alignof(Pair32) == 4,
               "natural pair has 4-byte alignment");
_Static_assert(sizeof(PackedPair32) == 8 && _Alignof(PackedPair32) == 1,
               "packed pair has 1-byte alignment");
_Static_assert(sizeof(AlignedPair32) == 8 && _Alignof(AlignedPair32) == 8,
               "overaligned pair has 8-byte alignment");

unsigned int accept_pair(unsigned int tag, ...);

unsigned int call_natural(Pair32 pair) {
    return accept_pair(1U, pair, 0x51U);
}
unsigned int call_packed(PackedPair32 pair) {
    return accept_pair(2U, pair, 0x52U);
}
unsigned int call_aligned(AlignedPair32 pair) {
    return accept_pair(3U, pair, 0x53U);
}
unsigned int call_wide_scalar(unsigned long long value) {
    return accept_pair(4U, value, 0x54U);
}

/* Pack-1 and pack-8 C types also exercise CoreObject layout, independent
 * of the register-pair selection. In the callee the second parameter may
 * occupy a frame offset of one byte. */
typedef struct __attribute__((packed)) { unsigned char value; } OneByte;
PackedPair32 return_after_byte(OneByte lead, PackedPair32 pair) {
    pair.low += lead.value;
    return pair;
}
PackedPair32 call_after_byte(OneByte lead, PackedPair32 pair) {
    return return_after_byte(lead, pair);
}
