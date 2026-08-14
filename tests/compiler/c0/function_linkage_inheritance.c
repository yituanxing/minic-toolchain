static inline int read_timer_like(unsigned long *value) {
    *value = 7UL;
    return 0;
}

int read_timer_like(unsigned long *value);

int main(void) {
    unsigned long value = 0UL;
    return read_timer_like(&value) == 0 && value == 7UL ? 0 : 1;
}
