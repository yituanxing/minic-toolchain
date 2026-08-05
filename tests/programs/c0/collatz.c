int main(void)
{
    int value = 27;
    int steps = 0;

    while (value != 1) {
        if (value % 2 == 0)
            value = value / 2;
        else
            value = value * 3 + 1;
        steps = steps + 1;
    }
    return steps;
}
