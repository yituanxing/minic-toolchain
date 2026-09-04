enum wide;
extern enum wide global_wide;
enum wide read_wide(enum wide value);

enum wide {
    WIDE_VALUE = 1ULL << 40,
};

enum wide global_wide = WIDE_VALUE;

enum wide read_wide(enum wide value) {
    return value;
}

unsigned long use_wide(void) {
    return (unsigned long)read_wide(global_wide);
}
