typedef unsigned int u32;
#if defined(BAD_SIZE)
typedef u32 __attribute__((vector_size(3))) illegal_vector;
#elif defined(BAD_ELEMENT)
struct record { u32 x; };
typedef struct record __attribute__((vector_size(16))) illegal_vector;
#elif defined(BAD_NONCONST)
extern int size_in_bytes;
typedef u32 __attribute__((vector_size(size_in_bytes))) illegal_vector;
#elif defined(UNSUPPORTED_WIDTH)
/* Valid GNU C, but MiniC x86 intentionally admits <=16B storage until the
 * complete 256/512-bit layout and ABI are separately proven. */
typedef u32 __attribute__((vector_size(32))) illegal_vector;
#else
#error Define one vector_size negative test mode
#endif
illegal_vector source_object;
