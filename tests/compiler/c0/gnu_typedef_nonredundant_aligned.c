typedef int over_aligned_int __attribute__((aligned(16)));

over_aligned_int read_over_aligned(over_aligned_int value) {
    return value;
}
