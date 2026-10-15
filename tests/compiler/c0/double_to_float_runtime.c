int main(void) {
    double source = 1.5;
    float narrowed = (float)source;
    double widened = (double)narrowed;

    return widened == 1.5 ? 0 : 1;
}
