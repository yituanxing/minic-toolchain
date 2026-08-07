int main(void)
{
    int values[4];
    int *pointer;
    int index;

    values[0] = 0;
    values[1] = 0;
    values[2] = 0;
    values[3] = 9;
    pointer = &values[0];
    index = 0;

    for (; index < 3; (void)pointer++, index++) {
        *pointer = index + 1;
    }

    if (values[0] != 1) {
        return 1;
    }
    if (values[1] != 2) {
        return 2;
    }
    if (values[2] != 3) {
        return 3;
    }
    if (values[3] != 9) {
        return 4;
    }
    if (index != 3) {
        return 5;
    }
    if (pointer != &values[3]) {
        return 6;
    }
    return 0;
}
