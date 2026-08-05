int main(void)
{
    unsigned value;
    unsigned quotient;
    unsigned remainder;

    value = 0 - 1;
    quotient = value / 65535;
    remainder = value % 251;
    if (value > 1) {
        return (quotient + remainder) % 251;
    }
    return 1;
}
