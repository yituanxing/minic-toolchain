int builtin_alloca_probe(unsigned long n);

int main(void) {
    if (builtin_alloca_probe(1UL) != 0)
        return 11;
    if (builtin_alloca_probe(37UL) != 0)
        return 12;
    if (builtin_alloca_probe(64UL) != 0)
        return 13;
    return 0;
}
