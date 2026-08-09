typedef union WideCounter {
    __extension__ unsigned long long value64;
    struct {
        unsigned low;
        unsigned high;
    } value32;
} WideCounter;

unsigned long long read_wide(WideCounter *counter) {
    return counter->value64;
}

int wide_size(void) {
    return sizeof(WideCounter);
}
