unsigned long native_save_fl(void) {
    unsigned long flags;
    asm volatile("# __raw_save_flags\n\tpushf ; pop %0"
                 : "=rm"(flags) : : "memory");
    return flags;
}
