typedef unsigned int u32;
#if defined(BAD_BOUND)
int incompatible(u32 (*data)[4]);
int incompatible(u32 (*data)[5]);
#elif defined(BAD_ELEMENT_QUALIFIER)
int incompatible(u32 (*data)[4]);
int incompatible(const u32 (*data)[4]);
#elif defined(BAD_NESTED_BOUND)
int incompatible(u32 (*data)[2][4]);
int incompatible(u32 (*data)[2][5]);
#else
#error select BAD_* test mode
#endif
