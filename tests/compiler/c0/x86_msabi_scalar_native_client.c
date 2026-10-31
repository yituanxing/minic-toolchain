/* MiniC SysV functions call foreign Win64 ABI functions by name and through
 * precisely typed EFI callbacks. No aggregate/variadic MS ABI is exercised. */
extern __attribute__((ms_abi)) long ms_scalar_zero(void);
extern __attribute__((ms_abi)) long ms_scalar_one(long);
extern __attribute__((ms_abi)) long ms_scalar_four(long, long, long, long);
extern __attribute__((ms_abi)) long ms_scalar_five(long, long, long, long, long);
extern __attribute__((ms_abi)) long ms_scalar_seven(
    long, long, long, long, long, long, long);
extern __attribute__((ms_abi)) double ms_scalar_mixed(
    double, long, float, long, double, long, double);

typedef long ms_zero_t(void);
typedef long ms_one_t(long);
typedef long ms_four_t(long, long, long, long);
typedef long ms_five_t(long, long, long, long, long);
typedef long ms_seven_t(long, long, long, long, long, long, long);
typedef double ms_mixed_t(double, long, float, long, double, long, double);

struct MSCallbacks {
    ms_zero_t __attribute__((ms_abi)) *zero;
    ms_one_t __attribute__((ms_abi)) *one;
    ms_four_t __attribute__((ms_abi)) *four;
    ms_five_t __attribute__((ms_abi)) *five;
    ms_seven_t __attribute__((ms_abi)) *seven;
    ms_mixed_t __attribute__((ms_abi)) *mixed;
};

long minic_ms_direct_zero(void) { return ms_scalar_zero(); }
long minic_ms_direct_one(void) { return ms_scalar_one(11); }
long minic_ms_direct_four(void) { return ms_scalar_four(1, 2, 3, 4); }
long minic_ms_direct_five(void) { return ms_scalar_five(1, 2, 3, 4, 5); }
long minic_ms_direct_seven(void) {
    return ms_scalar_seven(1, 2, 3, 4, 5, 6, 7);
}
double minic_ms_direct_mixed(void) {
    return ms_scalar_mixed(2.5, 11, 3.25f, 22, 4.5, 33, 5.5);
}

long minic_ms_indirect_zero(struct MSCallbacks *callbacks) {
    return callbacks->zero();
}
long minic_ms_indirect_one(struct MSCallbacks *callbacks) {
    return callbacks->one(11);
}
long minic_ms_indirect_four(struct MSCallbacks *callbacks) {
    return callbacks->four(1, 2, 3, 4);
}
long minic_ms_indirect_five(struct MSCallbacks *callbacks) {
    return callbacks->five(1, 2, 3, 4, 5);
}
long minic_ms_indirect_seven(struct MSCallbacks *callbacks) {
    return callbacks->seven(1, 2, 3, 4, 5, 6, 7);
}
double minic_ms_indirect_mixed(struct MSCallbacks *callbacks) {
    return callbacks->mixed(2.5, 11, 3.25f, 22, 4.5, 33, 5.5);
}
