typedef int over_aligned_int __attribute__((aligned(16)));

struct aligned_holder {
    char prefix;
    over_aligned_int value;
};

struct call_single_shape {
    unsigned long words[2];
};

typedef struct call_single_shape call_single_shape_t
    __attribute__((__aligned__(sizeof(struct call_single_shape))));

_Static_assert(__alignof__(over_aligned_int) == 16, "typedef minimum alignment");
_Static_assert(__builtin_offsetof(struct aligned_holder, value) == 16,
               "record field consumes typedef alignment");
_Static_assert(sizeof(struct aligned_holder) == 32, "record size consumes typedef alignment");
_Static_assert(sizeof(struct call_single_shape) == 16, "fixture shape size");
_Static_assert(__alignof__(call_single_shape_t) == 16, "sizeof expression alignment");

over_aligned_int read_over_aligned(over_aligned_int value) {
    return value;
}

unsigned long aligned_holder_size(void) {
    return sizeof(struct aligned_holder);
}

unsigned long call_single_alignment(void) {
    return __alignof__(call_single_shape_t);
}
