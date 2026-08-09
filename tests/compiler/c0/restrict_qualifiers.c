extern void *copy_gnu(void *__restrict destination, const void *__restrict source, unsigned long count);
extern void *copy_c(void *restrict destination, const void *restrict source, unsigned long count);

void *call_restrict_forms(void *destination, const void *source) {
    copy_gnu(destination, source, 4);
    return copy_c(destination, source, 4);
}
