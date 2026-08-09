const char *choose_const(int condition, const char *value) {
    return condition ? value : "bt";
}

volatile int *choose_volatile(int condition, int *plain_value, volatile int *volatile_value) {
    return condition ? plain_value : volatile_value;
}

const volatile int *choose_const_volatile(int condition,
                                          const int *const_value,
                                          volatile int *volatile_value) {
    return condition ? const_value : volatile_value;
}

const void *choose_const_void(int condition, const void *opaque, int *object) {
    return condition ? opaque : object;
}
