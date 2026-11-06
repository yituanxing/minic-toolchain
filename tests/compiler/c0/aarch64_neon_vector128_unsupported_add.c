typedef __Uint64x2_t vector128_u64;
typedef __Uint8x16_t vector128_u8;
static vector128_u64 a, result;
static vector128_u8 b;
/* The implementation must reject mixed lane identities, even for ADD. */
void unsupported_vector_add(void) { result = a + b; }
