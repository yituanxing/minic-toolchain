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

static unsigned int next_bit_like(unsigned int value) {
    return value + 2U;
}

int for_parenthesized_post_update(void) {
    unsigned int i;
    int total = 0;

    for ((i) = 0; (i) = next_bit_like(i), (i) < 8U; (i)++) {
        total += (int)i;
    }
    return total;
}

int for_comma_update(void) {
    int i = 0;
    int scale = 0;

    for (; i < 3; (i)++, scale += 2) {
    }
    return i + scale;
}
