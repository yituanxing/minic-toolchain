extern int core_m5_global;

int core_m5_global_load(void) {
    return core_m5_global;
}

void core_m5_global_store(int value) {
    core_m5_global = value;
}
