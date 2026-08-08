int main(void) {
    int values[3] = {10, 20, 30};
    int value = 4;
    int old = value++;
    int *pointer = values;
    int *old_pointer = pointer++;

    value--;
    return (old == 4 && value == 4 && old_pointer == values && pointer == values + 1 &&
            *pointer == 20)
               ? 0
               : 1;
}
