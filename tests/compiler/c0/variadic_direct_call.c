int verify_variadic(int tag, ...);

int main(void)
{
    char small;
    long wide;
    int value;

    small = 7;
    wide = 1234;
    value = 29;
    return verify_variadic(5, 11, small, wide, &value);
}
