/* MC-066: double* is a pointer, not a homogeneous FP aggregate member.
 * -DMC066_INTEGER_PTR changes only the pointee type as an ABI control. */
struct PointerRecord {
#ifdef MC066_INTEGER_PTR
    int *number;
#else
    double *number;
#endif
    unsigned char padding[24];
};
struct PointerRecord pass_pointer_record(struct PointerRecord x) { return x; }
