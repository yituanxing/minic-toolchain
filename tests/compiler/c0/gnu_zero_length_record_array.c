struct ZeroPad {
    char prefix;
    unsigned long reserved[0];
    char tail;
};

int zero_array_offset(void) {
    return __builtin_offsetof(struct ZeroPad, reserved);
}

int zero_tail_offset(void) {
    return __builtin_offsetof(struct ZeroPad, tail);
}

int zero_record_size(void) {
    return sizeof(struct ZeroPad);
}
