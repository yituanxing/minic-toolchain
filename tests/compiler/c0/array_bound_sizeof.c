enum {
    SIZE_WIDTH = 5,
    SIZE_TOTAL
};

typedef struct SignalSet {
    unsigned long words[16];
} SignalSet;

struct Sizes {
    unsigned char pointer_bytes[(sizeof(void *))];
    int arithmetic[sizeof(long) + 2 * sizeof(short)];
    int enum_bound[SIZE_TOTAL];
    int cast_bound[((int)(SIZE_TOTAL) + 1)];
    unsigned char literal_bytes[sizeof("\x1bLua") + sizeof("\x19\x93\r\n\x1a\n")];
    unsigned char aggregate_bytes[256 - sizeof(SignalSet)];
};

int read_sizes(struct Sizes *sizes) {
    return sizes->pointer_bytes[7] + sizes->arithmetic[11] + sizes->enum_bound[5] +
           sizes->cast_bound[6] + sizes->literal_bytes[11] + sizes->aggregate_bytes[127];
}
