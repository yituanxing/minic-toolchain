static unsigned long runtime_probe(unsigned long value) {
    return __builtin_constant_p(value) ? 11UL : value;
}

static unsigned long folded_probe(void) {
    return __builtin_constant_p(3 + 4) ? 22UL : 33UL;
}

int main(void) {
    unsigned long local = 5UL;

    if (__builtin_constant_p(7) != 1)
        return 1;
    if (__builtin_constant_p(local) != 0)
        return 2;
    if (runtime_probe(local) != 5UL)
        return 3;
    return folded_probe() == 22UL ? 0 : 4;
}
