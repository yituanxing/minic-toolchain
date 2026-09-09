long dynamic_stack_alloca_probe(unsigned long first_size,
                                unsigned long second_size);

long dynamic_stack_sum10(long a0,
                         long a1,
                         long a2,
                         long a3,
                         long a4,
                         long a5,
                         long a6,
                         long a7,
                         long a8,
                         long a9) {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9;
}

int main(void) {
    return dynamic_stack_alloca_probe(37, 23) == 119 ? 0 : 1;
}
