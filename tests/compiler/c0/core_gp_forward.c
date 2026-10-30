long minic_gp_chain(long a, long b, long c, long d, long e) {
    return (((a + b) * c) ^ d) + e;
}

long minic_gp_anti_chain(long a, long b, long c, long d) {
    long x = a + b;
    long y = x * c;
    return (y ^ d) + x;
}

static volatile long observed;

long minic_gp_volatile(long *out) {
    long x = observed;
    *out = x;
    return x;
}

int main(void) {
    long value = 0;
    observed = 17;
    if (minic_gp_chain(7, 3, 5, 12, 9) != 71 ||
        minic_gp_anti_chain(7, 3, 5, 12) != 72 ||
        minic_gp_volatile(&value) != 17 || value != 17)
        return 1;
    return 42;
}
