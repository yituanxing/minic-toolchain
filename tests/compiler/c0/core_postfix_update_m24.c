unsigned int core_m24_postdec_value(unsigned int *value) {
    return (*value)--;
}

unsigned int core_m24_countdown(unsigned int words) {
    unsigned int count = 0;
    while (words--) {
        count++;
    }
    return count;
}

unsigned short *core_m24_pointer_increment(unsigned short *pointer) {
    pointer++;
    return pointer;
}

unsigned short *core_m24_pointer_decrement(unsigned short *pointer) {
    pointer--;
    return pointer;
}

static void core_m24_touch(unsigned short *pointer) {
    *pointer = *pointer;
}

void core_m24_swab16_shape(unsigned short *buf, unsigned int words) {
    while (words--) {
        core_m24_touch(buf);
        buf++;
    }
}
