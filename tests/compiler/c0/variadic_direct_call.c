int verify_variadic(int tag, ...);

int main(void)
{
    char small;
    long wide;
    int value;
    double precise;

    small = 7;
    wide = 1234;
    value = 29;
    precise = 2.5;
    return verify_variadic(5, 11, small, wide, &value, precise);
}
