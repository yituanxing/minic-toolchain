/* MC-048: GNU RV32 sign-extends the raw 32-bit pointer representation for
 * direct pointer-to-64-bit conversions, even for an unsigned 64-bit target.
 * A cast through unsigned long instead zero-extends the same pointer bits.
 * The bit-31=1 control distinguishes these two C cast paths. */
typedef unsigned long long u64;
typedef signed long long s64;

unsigned long pointer_to_ulong(const void *p) { return (unsigned long)p; }
signed long pointer_to_slong(const void *p) { return (signed long)p; }
u64 pointer_to_u64(const void *p) { return (u64)p; }
s64 pointer_to_s64(const void *p) { return (s64)p; }
u64 pointer_via_ulong(const void *p) { return (u64)(unsigned long)p; }
void *u64_to_pointer(u64 x) { return (void *)x; }
void *s64_to_pointer(s64 x) { return (void *)x; }

u64 unsigned_round_trip(const void *p) {
    return (u64)u64_to_pointer(pointer_to_u64(p));
}
s64 signed_round_trip(const void *p) {
    return (s64)s64_to_pointer(pointer_to_s64(p));
}

int main(void) {
    const void *p = (const void *)(unsigned long)0xf1234567UL;
    const void *low = (const void *)(unsigned long)0x71234567UL;
    u64 u = pointer_to_u64(p);
    s64 s = pointer_to_s64(p);
    u64 expected = sizeof(void *) == 4U ? 0xfffffffff1234567ULL : 0x00000000f1234567ULL;
    if (u != expected) return 1;
    if (s != (s64)expected) return 2;
    if (pointer_via_ulong(p) != 0x00000000f1234567ULL) return 3;
    if (pointer_to_ulong(p) != 0xf1234567UL) return 10;
    if (pointer_to_slong(p) != (signed long)0xf1234567UL) return 11;
    if (unsigned_round_trip(p) != u) return 4;
    if (signed_round_trip(p) != s) return 5;
    if (u64_to_pointer(u) != p || s64_to_pointer(s) != p) return 6;
    if (pointer_to_u64(low) != 0x0000000071234567ULL) return 8;
    if (pointer_to_s64(low) != (s64)0x0000000071234567ULL) return 9;
    return 7;
}
