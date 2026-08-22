extern int core_m5b_global;

int core_m5b_equal(int expected) {
    return core_m5b_global == expected;
}

void core_m5b_set_if_equal(int value, int expected) {
    if (core_m5b_global == expected)
        core_m5b_global = value;
}
