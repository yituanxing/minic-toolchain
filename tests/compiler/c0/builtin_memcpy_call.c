void *copy_with_builtin(char *dest, const char *src, unsigned long length) {
    return __builtin_memcpy(dest, src, length);
}

void *memcpy(void *dest, const void *src, unsigned long length);

void copy_and_discard(char *dest, const char *src, unsigned long length) {
    (void)__builtin_memcpy(dest, src, length);
}
