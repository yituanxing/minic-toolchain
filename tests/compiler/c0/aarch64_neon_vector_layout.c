/* The first executable-vector milestone must agree with GCC's actual
 * AAPCS64 declaration and object layout, before any NEON operation is added.
 * Do not replace a vector with an integer or a record as a false shortcut. */
typedef __Uint8x16_t uint8x16_t;
typedef __Uint64x2_t uint64x2_t;
typedef __Int8x16_t int8x16_t;
typedef __Poly8x16_t poly8x16_t;

_Static_assert(sizeof(uint8x16_t) == 16, "NEON byte vector storage");
_Static_assert(_Alignof(uint8x16_t) == 16, "NEON byte vector alignment");
_Static_assert(sizeof(uint64x2_t) == 16, "NEON 64-bit lane vector storage");
_Static_assert(_Alignof(uint64x2_t) == 16, "NEON 64-bit lane vector alignment");
_Static_assert(sizeof(int8x16_t) == 16, "NEON signed byte vector storage");
_Static_assert(_Alignof(int8x16_t) == 16, "NEON signed byte vector alignment");
_Static_assert(sizeof(poly8x16_t) == 16, "NEON polynomial byte vector storage");
_Static_assert(_Alignof(poly8x16_t) == 16, "NEON polynomial byte vector alignment");

struct neon_vector_layout {
    unsigned char tag;
    uint8x16_t first;
    uint64x2_t second;
};
_Static_assert(__builtin_offsetof(struct neon_vector_layout, first) == 16,
               "NEON first field alignment");
_Static_assert(__builtin_offsetof(struct neon_vector_layout, second) == 32,
               "NEON second field alignment");
_Static_assert(sizeof(struct neon_vector_layout) == 48,
               "NEON record layout");

int main(void) { return 0; }
