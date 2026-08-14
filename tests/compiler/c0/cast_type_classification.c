union Box {
    int value;
    long wide;
};

int read_union_cast(void *pointer) {
    return ((union Box *)pointer)->value;
}

int read_volatile_cast(int *pointer) {
    return *((volatile int *)pointer);
}

int narrow_short(int value) {
    return (int)((short)value);
}
