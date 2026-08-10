struct full_unit_pad {
    char tag;
    int :32;
    char tail;
};

struct zero_width_barrier {
    char tag;
    int :0;
    char tail;
};

unsigned long full_unit_tail_offset(void) {
    return __builtin_offsetof(struct full_unit_pad, tail);
}

unsigned long zero_width_tail_offset(void) {
    return __builtin_offsetof(struct zero_width_barrier, tail);
}

int main(void) {
    return full_unit_tail_offset() == 8UL && zero_width_tail_offset() == 4UL ? 0 : 1;
}
