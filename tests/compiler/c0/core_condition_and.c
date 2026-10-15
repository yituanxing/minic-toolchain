int core_m12_rhs(void);

int core_m12_short_circuit_and(int left) {
    if (left && core_m12_rhs())
        return 7;
    return 3;
}

int core_m12_wrapped_pointer_and(int *left, int *middle, int *right) {
    if (__builtin_expect(!!(left == middle && middle == right), 1))
        return 11;
    return 5;
}
