long expect_value(int value) {
    return __builtin_expect(value != 0, 1);
}

long expect_zero(int value) {
    return __builtin_expect(value == 0, 0 + 1 - 1);
}
