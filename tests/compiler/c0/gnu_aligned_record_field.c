struct AlignedField {
    char prefix;
    unsigned long long values[2] __attribute__((__aligned__(16)));
    char tail;
};

int aligned_values_offset(void) {
    return __builtin_offsetof(struct AlignedField, values);
}

int aligned_tail_offset(void) {
    return __builtin_offsetof(struct AlignedField, tail);
}

int aligned_record_size(void) {
    return sizeof(struct AlignedField);
}
