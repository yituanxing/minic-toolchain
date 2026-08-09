int for_compound_initializer(void) {
    int i = 7;
    int total = 0;

    for (i -= 2; i > 0; i--) {
        total += i;
    }
    return total;
}
