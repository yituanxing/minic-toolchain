int main(void)
{
    int values[5];
    int index = 0;
    int total = 0;

    while (index < 5) {
        values[index] = index * 3 + 1;
        index = index + 1;
    }

    index = 0;
    while (index < 5) {
        total = total + values[index];
        index = index + 1;
    }

    return total;
}
