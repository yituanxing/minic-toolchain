#define __weak __attribute__((weak))
extern const void __start_notes __weak;
extern const void __stop_notes __weak;
extern void *memcpy(void *dst, const void *src, unsigned long n);

unsigned long notes_size(void) {
    return (unsigned long)(&__stop_notes - &__start_notes);
}

void notes_copy(char *buf, long off, unsigned long count) {
    memcpy(buf, &__start_notes + off, count);
}
