int minic_portable_add_overflow_predicate(int a, int b) {
    return __builtin_add_overflow_p(a, b, (int)0);
}

int minic_portable_mul_overflow_predicate(unsigned int a, unsigned int b) {
    return __builtin_mul_overflow_p(a, b, (unsigned int)0);
}
