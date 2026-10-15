int core_m9_rhs(void);

int core_m9_short_circuit_or(int left) {
    if (left || core_m9_rhs())
        return 7;
    return 3;
}
