typedef struct MiniNode {
    int value;
} MiniNode;

static long long *bump_counter(void) {
    static long long counter = 0;
    counter += 1;
    return &counter;
}

static void *null_pointer_address(void) {
    static const MiniNode *const null_value = (void *)0;
    return (void *)&null_value;
}

int read_counter(void) {
    long long *first = bump_counter();
    long long *second = bump_counter();
    return first == second && *second == 2 && null_pointer_address() != (void *)0;
}
