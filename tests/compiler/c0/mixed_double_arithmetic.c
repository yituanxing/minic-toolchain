double compute(double value) {
    const double q = 0.25;
    const double scaled = value * (1 + q);
    return scaled / 2 - 3;
}

int main(void) {
    return compute(8.0) == 2.0 ? 0 : 1;
}
