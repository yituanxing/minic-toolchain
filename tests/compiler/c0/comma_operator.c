int comma_growth(void) {
    return ((1),
            (1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12 + 13 + 14 + 15 + 16 + 17 +
             18 + 19 + 20));
}

int comma_value(int *target) {
    int value = 1;
    ((void)(value), ((void)0));
    return ((*target = value + 2), *target);
}

int comma_conditions(int *target) {
    int value = 0;

    while (((void)(*target = *target + 1)), (value < 1)) {
        value += 1;
    }
    if (((void)(*target = *target + 1)), (value == 1)) {
        return *target;
    }
    return 0;
}

int comma_expression_statement(void)
{
    int value = 0;

    (void)(value += 1), (void)(value += 2), (void)(value += 4);
    return value;
}
