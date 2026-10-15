struct CoreHybridLayout {
    char prefix;
    int value;
};

int core_hybrid_core(int value) {
    return -value;
}

int core_hybrid_call_target(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

int core_hybrid_call(int value) {
    return core_hybrid_call_target(value, 2, 3, 4, 5, 6);
}

int core_hybrid_field(struct CoreHybridLayout *item) {
    item->value = 53;
    return 9;
}

int core_hybrid_fallback_load(int *value) {
    return *value;
}

int core_hybrid_indirect_target(int value) {
    return value + 4;
}

int core_hybrid_fallback_indirect(int (*callee)(int), int value) {
    return callee(value);
}
