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

static int sectioned_static_locals(void) {
    static int scalar __attribute__((__section__(".minic.static.scalar"))) = 3;
    static char fixed[8] __attribute__((__section__(".minic.static.fixed")));
    static char inferred[] __attribute__((__section__(".minic.static.inferred"))) = "ok";
    static int first __attribute__((__section__(".minic.static.first"))), second;

    return scalar + fixed[0] + inferred[0] + first + second;
}

int read_sectioned_static_locals(void) {
    return sectioned_static_locals();
}
