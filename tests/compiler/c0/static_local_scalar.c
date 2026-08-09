static long long *bump_counter(void) {
    static long long counter = 0;
    counter += 1;
    return &counter;
}

int read_counter(void) {
    long long *first = bump_counter();
    long long *second = bump_counter();
    return first == second && *second == 2;
}
