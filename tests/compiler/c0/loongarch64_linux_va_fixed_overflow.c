/* Original Linux 6.6.143 drm_universal_plane_init and config_item_set_name
 * require GNU va_start with 9/10 named arguments and independent cursors. */
typedef __builtin_va_list minic_va_list;

long loong_va9(long a, long b, long c, long d, long e, long f,
               long g, long h, long i, ...) {
    minic_va_list ap;
    long x, y;
    __builtin_va_start(ap, i);
    x = __builtin_va_arg(ap, long);
    y = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return a + i + x * 3 + y;
}

long loong_va10(long a, long b, long c, long d, long e, long f,
                long g, long h, long i, long j, ...) {
    minic_va_list ap;
    long x;
    __builtin_va_start(ap, j);
    x = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return b + j + x;
}

long loong_va_two_starts(long tag, const char *format, ...) {
    minic_va_list first, second, copied;
    long a, b, c;
    __builtin_va_start(first, format);
    __builtin_va_start(second, format);
    __builtin_va_copy(copied, first);
    a = __builtin_va_arg(first, long);
    b = __builtin_va_arg(second, long);
    c = __builtin_va_arg(copied, long);
    __builtin_va_end(first);
    __builtin_va_end(second);
    __builtin_va_end(copied);
    return tag + a + b + c;
}

int main(void) {
    if (loong_va9(1, 2, 3, 4, 5, 6, 7, 8, 9, 17, 23) != 84)
        return 1;
    if (loong_va10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 19) != 31)
        return 2;
    if (loong_va_two_starts(3, "x", 11, 17) != 36)
        return 3;
    return 0;
}
