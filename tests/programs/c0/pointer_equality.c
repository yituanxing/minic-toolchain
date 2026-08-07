int main(void)
{
    int first;
    int second;
    int *pointer;
    int *other;
    const int *const_pointer;

    first = 11;
    second = 29;
    pointer = &first;
    other = &second;
    const_pointer = pointer;

    if (pointer == 0) {
        return 1;
    }
    if (0 == pointer) {
        return 2;
    }
    if (pointer != &first) {
        return 3;
    }
    if (pointer == other) {
        return 4;
    }
    if (pointer != const_pointer) {
        return 5;
    }
    if (pointer != (void *)pointer) {
        return 6;
    }
    if (pointer == (void *)0) {
        return 7;
    }

    pointer = (void *)0;
    if (pointer != 0) {
        return 8;
    }
    if (0 != pointer) {
        return 9;
    }
    if (pointer != (void *)0) {
        return 10;
    }
    return 0;
}
