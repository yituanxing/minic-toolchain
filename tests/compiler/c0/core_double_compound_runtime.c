int main(void)
{
    double value = 1.5;

    value += 2;
    if (value != 3.5) return 1;
    value *= 2.0;
    if (value != 7.0) return 2;
    value -= 1;
    if (value != 6.0) return 3;
    value /= 3.0;
    return value == 2.0 ? 0 : 4;
}
