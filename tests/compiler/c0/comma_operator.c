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
