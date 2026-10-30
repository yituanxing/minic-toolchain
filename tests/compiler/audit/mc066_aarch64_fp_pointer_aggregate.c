/* MC-066: double* is a pointer, not a homogeneous FP aggregate member.
 * Controls cover integer pointees, arrays of FP pointers, and actual FP
 * aggregate values, while preserving one 32-byte record ABI shape. */
struct PointerRecord {
#ifdef MC066_FLOAT_VALUES
    double first;
    double second;
    double third;
    double fourth;
#elif defined(MC066_POINTER_ARRAY)
    double *numbers[2];
    unsigned char padding[16];
#elif defined(MC066_INTEGER_PTR)
    int *number;
    unsigned char padding[24];
#else
    double *number;
    unsigned char padding[24];
#endif
};
struct PointerRecord pass_pointer_record(struct PointerRecord x) { return x; }
