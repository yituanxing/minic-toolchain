typedef union {
    unsigned long value;
    void *pointer;
} mixed_arg __attribute__((__transparent_union__));
