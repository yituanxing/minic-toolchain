/* Exercise private Core SSA spills and intervening side-effect barriers. */
long mini_stack_forward_chain(long a, long b, long c) {
    long first = a + b;
    long second = first + c;
    return second + first;
}

long mini_stack_forward_call(long a, long b, long (*f)(long)) {
    long first = a + b;
    return f(first) + first;
}

long mini_stack_forward_asm(long a, long b) {
    long first = a + b;
    __asm__ __volatile__("nop" ::: "memory");
    return first + b;
}

long mini_stack_forward_branch(long a, long b, int choose) {
    long first = a + b;
    if (choose) return first + b;
    return first + a;
}

/* A large private frame exercises the 12-bit displacement expansion path. */
long mini_stack_forward_large(long a, long b) {
    volatile long pad[640];
    pad[0] = a;
    pad[639] = b;
    return pad[0] + pad[639] + a + b;
}
