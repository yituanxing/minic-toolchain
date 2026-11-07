unsigned int loongson_cpu_get_prid(void) {
    unsigned int prid;
    __asm__ volatile("cpucfg %0, %1\n\t" : "=&r"(prid) : "r"(0));
    return prid;
}
unsigned int loongson_cpu_get_cfg(unsigned int index) {
    unsigned int cfg;
    __asm__ volatile("cpucfg %0, %1\n\t" : "=&r"(cfg) : "r"(index));
    return cfg;
}
