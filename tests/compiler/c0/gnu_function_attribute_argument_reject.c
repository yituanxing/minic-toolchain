#ifdef TOO_MANY_ALLOC_SIZE_ARGUMENTS
extern void *bad_alloc_size(unsigned long first, unsigned long second, unsigned long third)
    __attribute__((__alloc_size__(1, 2, 3)));
#else
extern void *bad_alloc_size(unsigned long count) __attribute__((__alloc_size__()));
#endif
