static __attribute__((__unused__)) const int class_irq_is_conditional = 0;

static inline __attribute__((__always_inline__)) int class_irq_add(int value) {
    return value + 1;
}

int main(void) {
    return class_irq_is_conditional == 0 && class_irq_add(6) == 7 ? 0 : 1;
}
