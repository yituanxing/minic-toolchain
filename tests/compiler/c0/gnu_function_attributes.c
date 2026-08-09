extern void *memory_copy(void *__restrict destination,
                         const void *__restrict source,
                         unsigned long count)
    __attribute__((__nothrow__, __leaf__))
    __attribute__((__nonnull__(1, 2)))
    __attribute__((__access__(__write_only__, 1, 3)));

extern int memory_compare(const void *left, const void *right, unsigned long count)
    __attribute__((__nothrow__, __pure__))
    __attribute__((__nonnull__(1, 2)));

int call_attribute_functions(void *destination, const void *source) {
    memory_copy(destination, source, 4);
    return memory_compare(destination, source, 4);
}
