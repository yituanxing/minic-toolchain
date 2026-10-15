static unsigned int core_m25_touch(unsigned int *value) {
    *value = *value + 3U;
    return *value;
}

void core_m25_discard_pointer(unsigned int *value) {
    (void)(value);
}

void core_m25_discard_call(unsigned int *value) {
    (void)core_m25_touch(value);
}

unsigned int *core_m25_le32_shape(unsigned int *buf, unsigned int words) {
    while (words--) {
        do {
            (void)(buf);
        } while (0);
        buf++;
    }
    return buf;
}
