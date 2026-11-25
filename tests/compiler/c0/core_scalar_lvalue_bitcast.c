struct CoreBitcastPair {
    int offset;
    int value;
};

void *core_offset_to_ptr(const int *offset) {
    return (void *)((unsigned long)offset + *offset);
}

int core_pointer_read(const int *value) {
    return *value;
}

int core_member_read(struct CoreBitcastPair *pair) {
    return pair->value;
}

unsigned long core_pointer_bits(const void *value) {
    return (unsigned long)value;
}
