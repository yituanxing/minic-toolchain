int core_hybrid_core(int value) {
    return -value;
}

int core_hybrid_fallback_load(int *value) {
    return *value;
}
