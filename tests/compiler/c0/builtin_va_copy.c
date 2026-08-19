static int copy_cursor(int fixed, ...) {
    char *source;
    char *copy;

    __builtin_va_start(source, fixed);
    __builtin_va_copy(copy, source);
    if (copy != source) {
        return 7;
    }
    __builtin_va_end(copy);
    __builtin_va_end(source);
    return 0;
}

int main(void) {
    return copy_cursor(1, 2, 3);
}
