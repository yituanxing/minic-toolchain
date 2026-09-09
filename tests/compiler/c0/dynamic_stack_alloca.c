extern long dynamic_stack_sum10(long a0,
                                long a1,
                                long a2,
                                long a3,
                                long a4,
                                long a5,
                                long a6,
                                long a7,
                                long a8,
                                long a9);

long dynamic_stack_alloca_probe(unsigned long first_size,
                                unsigned long second_size) {
    long fixed;
    unsigned char *first;
    unsigned char *second;

    fixed = 40;
    first = (unsigned char *)__builtin_alloca(first_size);
    first[0] = 3;
    first[first_size - 1] = 5;

    second = (unsigned char *)__builtin_alloca(second_size);
    second[0] = 7;
    second[second_size - 1] = 9;

    return fixed + first[0] + first[first_size - 1] +
           second[0] + second[second_size - 1] +
           dynamic_stack_sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
}
