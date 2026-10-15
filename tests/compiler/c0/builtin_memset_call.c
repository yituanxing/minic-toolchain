void *zero_with_builtin(char *dest, unsigned long length) {
    return __builtin_memset(dest, 0, length);
}

void *memset(void *dest, int value, unsigned long length);

void fill_and_discard(char *dest, unsigned long length) {
    (void)__builtin_memset(dest, 0x5a, length);
}
