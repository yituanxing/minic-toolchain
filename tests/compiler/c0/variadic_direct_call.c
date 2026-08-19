int verify_variadic(int tag, ...);

int main(void) {
    char small;
    long wide;
    int value;
    double precise;

    small = 7;
    wide = 1234;
    value = 29;
    precise = 2.5;
    return verify_variadic(5,
                           11,
                           small,
                           wide,
                           &value,
                           precise,
                           61,
                           62,
                           63,
                           64,
                           65,
                           66,
                           67,
                           68,
                           69,
                           70,
                           71,
                           72,
                           73,
                           74);
}
