int main(void) {
    const double source = 3.25;
    double value = 0.0;

    value = ((void)0, source);
    if (value != 3.25) {
        return 1;
    }

    value = ((void)(value + 1.0), source);
    return value == 3.25 ? 0 : 2;
}
