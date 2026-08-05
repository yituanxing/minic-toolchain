int main(void)
{
    int values[4];
    int *base = &values[0];
    int index = 0;
    int first = 3;
    int second = 9;
    int *slots[2];
    int **slot_base = &slots[0];

    while (index < 4) {
        *(base + index) = index * 5 + 2;
        index = index + 1;
    }

    *(slot_base + 0) = &first;
    *(1 + slot_base) = &second;

    return *(base + 3) + *((&values[3]) - 2) +
           **slot_base + **(slot_base + 1);
}
