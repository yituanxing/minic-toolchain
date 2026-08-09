int main(void) {
    double a = 3.0;
    double b = 2.0;
    double maximum = (a > b) ? a : b;
    double minimum = (a < b) ? a : b;

    if (maximum != 3.0) {
        return 1;
    }
    if (minimum != 2.0) {
        return 2;
    }
    return 0;
}
