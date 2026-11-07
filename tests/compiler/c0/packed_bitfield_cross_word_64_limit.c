/* Admitted by GNU C, but currently exceeds Core's 64-bit extraction
 * container (64-bit field starting at a nonzero bit offset takes 9 bytes).
 * This must fail closed until a split high/low transport is available. */
struct __attribute__((packed)) PackedNineBytes {
    unsigned int first : 1;
    unsigned long long value : 64;
};

_Static_assert(sizeof(struct PackedNineBytes) == 9U, "GNU 65-bit packed record");

unsigned long long read_nine(const struct PackedNineBytes *p) {
    return p->value;
}
