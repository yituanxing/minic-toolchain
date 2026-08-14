struct AlignedField {
    char prefix;
    unsigned long long values[2] __attribute__((__aligned__(1 << (4))));
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


typedef unsigned long long __u64;

/* Linux uapi sched.h shape: field alignment between type specifier and name. */
struct PrefixAlignedField {
    char prefix;
    __u64 __attribute__((aligned(8))) flags;
    char tail;
};

int prefix_aligned_flags_offset(void) {
    return __builtin_offsetof(struct PrefixAlignedField, flags);
}

int prefix_aligned_tail_offset(void) {
    return __builtin_offsetof(struct PrefixAlignedField, tail);
}

/* Prefix and suffix placements must share one consumer and merge by max. */
struct MixedAlignedField {
    char prefix;
    unsigned long long __attribute__((aligned(8))) value
        __attribute__((aligned(16)));
    char tail;
};

int mixed_aligned_value_offset(void) {
    return __builtin_offsetof(struct MixedAlignedField, value);
}

int mixed_aligned_tail_offset(void) {
    return __builtin_offsetof(struct MixedAlignedField, tail);
}

int mixed_aligned_record_size(void) {
    return sizeof(struct MixedAlignedField);
}
