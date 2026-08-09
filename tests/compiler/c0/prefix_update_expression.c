int read_with_prefix_updates(int *values) {
    int pc = 0;
    int first = values[++pc];
    int second = values[--pc];
    return first + second + pc;
}

int *advance_pointer(int *pointer) {
    return ++pointer;
}

int *retreat_pointer(int *pointer) {
    return --pointer;
}
