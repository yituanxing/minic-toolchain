int comma_value(int *target) {
    int value = 1;
    ((void)(value), ((void)0));
    return ((*target = value + 2), *target);
}
