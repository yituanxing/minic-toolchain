typedef unsigned int u32;
typedef u32 v4 __attribute__((vector_size(16)));
typedef u32 v2 __attribute__((vector_size(8)));

#if defined(BAD_ARITHMETIC)
void forbidden(void) { v4 a, b, c; a = b + c; }
#elif defined(BAD_VOLATILE)
void forbidden(void) { volatile v4 a, b; a = b; }
#elif defined(BAD_EIGHT_BYTES)
void forbidden(void) { v2 a, b; a = b; }
#elif defined(BAD_BYVALUE)
v4 forbidden(v4 value) { return value; }
#else
#error Select one unsupported vector operation
#endif
