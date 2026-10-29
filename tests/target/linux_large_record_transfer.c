/* Linux task_struct-shaped aggregates exceed the old 2048-byte Core
 * transfer limit. Both direct copy and value materialization must have
 * bounded native loops, not a multi-megabyte per-byte assembly expansion. */
struct linux_large_record {
    unsigned char state[8192];
};
struct linux_small_record {
    unsigned char state[32];
};

void linux_large_record_copy(struct linux_large_record *destination,
                             const struct linux_large_record *source) {
    *destination = *source;
}

void linux_large_record_snapshot(struct linux_large_record *destination,
                                 const struct linux_large_record *source) {
    struct linux_large_record copy = *source;
    *destination = copy;
}

void linux_small_record_copy(struct linux_small_record *destination,
                             const struct linux_small_record *source) {
    *destination = *source;
}
