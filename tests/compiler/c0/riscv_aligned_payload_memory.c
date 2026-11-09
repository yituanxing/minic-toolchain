/* Statically proven Core local address alignment may select integer word
 * payload transfers even under RV32I/RV64I's soft-float ABI. A pointer
 * parameter and a GNU packed field do not carry that proof. */
struct __attribute__((packed)) PackedDouble {
    unsigned char tag;
    double payload;
};

__attribute__((noinline)) double aligned_local_payload(double incoming) {
    double local = incoming;
    return local;
}

__attribute__((noinline)) double unknown_pointer_payload(const double *p) {
    return *p;
}

__attribute__((noinline)) double packed_field_payload(
    const struct PackedDouble *p) {
    return p->payload;
}
