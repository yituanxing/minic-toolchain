typedef _Bool bool;

enum {
    false = 0,
    true = 1
};

static __attribute__((__unused__)) const bool class_irq_is_conditional = false;

static inline __attribute__((__always_inline__)) int class_irq_add(int value) {
    return value + 1;
}

int main(void) {
    return class_irq_is_conditional == false && class_irq_add(6) == 7 ? 0 : 1;
}
