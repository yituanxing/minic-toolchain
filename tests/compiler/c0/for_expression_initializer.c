int for_compound_initializer(void) {
    int i = 7;
    int total = 0;

    for (i -= 2; i > 0; i--) {
        total += i;
    }
    return total;
}

int for_comma_initializer(void) {
    int i = 9;
    int scale = 9;

    for (i = 0, scale = 1; i < 3; i++) {
        scale *= 2;
    }
    return i + scale;
}
