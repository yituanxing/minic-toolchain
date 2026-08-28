int main(void) {
    static void *const dispatch[] = { &&zero, &&one, &&two };
    int selector = 2;

    goto *dispatch[selector];

zero:
    return 10;
one:
    return 11;
two:
    return 0;
}
