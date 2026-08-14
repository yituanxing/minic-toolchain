extern void *memory_copy(void *__restrict destination,
                         const void *__restrict source,
                         unsigned long count)
    __attribute__((__nothrow__, __leaf__))
    __attribute__((__nonnull__(1, 2)))
    __attribute__((__access__(__write_only__, 1, 3)));

extern void *allocate_like(unsigned long count)
    __attribute__((__nothrow__, __malloc__));

extern void *allocate_sized(unsigned long count)
    __attribute__((__malloc__, __alloc_size__(1)));

extern void *allocate_matrix(unsigned long rows, unsigned long columns)
    __attribute__((__alloc_size__(1, 2)));

extern void *allocate_aligned(unsigned long count)
    __attribute__((__assume_aligned__((8)))) __attribute__((__malloc__));

extern void *allocate_aligned_offset(unsigned long count)
    __attribute__((assume_aligned(__alignof__(unsigned long long), 4)));

extern int stable_transform(int value) __attribute__((const));

extern void __attribute__((noreturn)) fatal_error(void);
extern int old_api(void) __attribute__((__deprecated__("use new_api instead")));

extern int memory_compare(const void *left, const void *right, unsigned long count)
    __attribute__((__nothrow__, __pure__))
    __attribute__((__nonnull__(1, 2)));

int call_attribute_functions(void *destination, const void *source) {
    void *allocated = allocate_like(4);
    void *sized = allocate_sized(4);
    void *matrix = allocate_matrix(2, 2);
    void *aligned = allocate_aligned(8);
    void *aligned_offset = allocate_aligned_offset(8);
    memory_copy(destination, source, 4);
    return allocated != (void *)0 && sized != (void *)0 && matrix != (void *)0 &&
           aligned != (void *)0 && aligned_offset != (void *)0 &&
           memory_compare(destination, source, 4) && stable_transform(1);
}
