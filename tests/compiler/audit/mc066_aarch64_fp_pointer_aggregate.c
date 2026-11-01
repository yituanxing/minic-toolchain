/* MC-066: a type's FLOAT/DOUBLE base_kind does not make its pointer an FP
 * leaf. Preserve 16/17/32/64-byte ABI shapes and verify nested indirections
 * separately from actual homogeneous FP/NEON value members. */
#ifndef MC066_BYTES
#define MC066_BYTES 32
#endif

typedef double *MC066_DoublePtr;
typedef double (*MC066_ArrayPtr)[4];
struct MC066_FPObject { double number; };
typedef struct MC066_FPObject *MC066_RecordPtr;

#if MC066_BYTES == 17 || defined(MC066_PACKED_POINTER) || \
    defined(MC066_PACKED_FLOAT_VALUES)
#define MC066_RECORD_ATTRIBUTE __attribute__((packed))
#else
#define MC066_RECORD_ATTRIBUTE
#endif

struct MC066_RECORD_ATTRIBUTE PointerRecord {
#ifdef MC066_FLOAT_VALUES
    double first;
    double second;
    double third;
    double fourth;
#elif defined(MC066_PACKED_FLOAT_VALUES)
    double first;
    double second;
    double third;
    double fourth;
#elif defined(MC066_POINTER_ARRAY)
    double *numbers[2];
    #if MC066_BYTES > 16
    unsigned char padding[MC066_BYTES - sizeof(double *) * 2];
    #endif
#elif defined(MC066_TYPEDEF_PTR)
    MC066_DoublePtr number;
    unsigned char padding[MC066_BYTES - sizeof(MC066_DoublePtr)];
#elif defined(MC066_POINTER_TO_ARRAY)
    MC066_ArrayPtr number;
    unsigned char padding[MC066_BYTES - sizeof(MC066_ArrayPtr)];
#elif defined(MC066_POINTER_TO_RECORD)
    MC066_RecordPtr number;
    unsigned char padding[MC066_BYTES - sizeof(MC066_RecordPtr)];
#elif defined(MC066_PACKED_POINTER)
    unsigned char prefix;
    double *number;
    unsigned char padding[MC066_BYTES - 1 - sizeof(double *)];
#elif defined(MC066_INTEGER_PTR)
    int *number;
    unsigned char padding[MC066_BYTES - sizeof(int *)];
#else
    double *number;
    unsigned char padding[MC066_BYTES - sizeof(double *)];
#endif
};
_Static_assert(sizeof(struct PointerRecord) == MC066_BYTES,
               "MC-066 probe must retain exact AArch64 record extent");

struct PointerRecord pass_pointer_record(struct PointerRecord x);

/* Compile the same source twice to cross the caller/callee ABI boundary.
 * Runtime mode is only used with the default double* member. */
#ifdef MC066_RUNTIME_CALLER
int main(void) {
    double datum = 7.0;
    struct PointerRecord input = {0};
    struct PointerRecord output;
    unsigned int i;
    input.number = &datum;
    for (i = 0U; i < sizeof(input.padding); ++i)
        input.padding[i] = (unsigned char)(i * 7U + 1U);
    output = pass_pointer_record(input);
    if (output.number != &datum) return 1;
    for (i = 0U; i < sizeof(input.padding); ++i)
        if (output.padding[i] != input.padding[i]) return 2;
    return 42;
}
#else
struct PointerRecord pass_pointer_record(struct PointerRecord x) { return x; }
#endif
