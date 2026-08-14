static long absolute_long(long value) {
    return __builtin_choose_expr(
        __builtin_types_compatible_p(typeof(value), signed long) ||
            __builtin_types_compatible_p(typeof(value), unsigned long),
        ({
            signed long copy = value;
            copy < 0 ? -copy : copy;
        }),
        (void)0);
}

static int absolute_int(int value) {
    return __builtin_choose_expr(
        __builtin_types_compatible_p(typeof(value), signed long),
        99L,
        __builtin_choose_expr(
            __builtin_types_compatible_p(typeof(value), signed int),
            ({
                signed int copy = value;
                copy < 0 ? -copy : copy;
            }),
            -1));
}

int main(void) {
    return absolute_long(-7L) == 7L && absolute_int(-5) == 5 ? 0 : 1;
}
