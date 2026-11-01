/* Between x87's 64 and IEEE binary128's 113 significant bits. This check
 * deliberately runs only on a target whose long double is binary128. */
int main(void) {
    volatile long double value = 1.000000000000000000000000000000001L;
    volatile long double one = 1.0L;
    return value != one ? 42 : 1;
}
