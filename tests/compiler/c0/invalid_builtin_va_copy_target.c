int bad_va_copy_target(int fixed, ...) {
    char *source;
    int target;

    __builtin_va_start(source, fixed);
    __builtin_va_copy(target, source);
    return 0;
}
