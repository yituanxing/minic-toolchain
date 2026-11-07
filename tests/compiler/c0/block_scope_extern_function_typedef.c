/* Original Linux AArch64: typedef is already a function, not a function
 * pointer object. Its block-scope extern must keep the canonical symbol
 * and ABI prototype without inventing a function-valued storage object. */
typedef void (ttbr_replace_func)(unsigned long);
typedef unsigned long (read_counter_func)(void);

unsigned long select_replacer(unsigned long value) {
    extern ttbr_replace_func idmap_cpu_replace_ttbr1;
    extern read_counter_func read_counter;
    ttbr_replace_func *replace = idmap_cpu_replace_ttbr1;
    replace(value);
    return read_counter();
}
