/* MC-089: compile this TU with MiniC; the harness is a separate GNU TU. */
int mc089_less(long double a, long double b) { return a < b; }
int mc089_less_equal(long double a, long double b) { return a <= b; }
int mc089_greater(long double a, long double b) { return a > b; }
int mc089_greater_equal(long double a, long double b) { return a >= b; }
int mc089_equal(long double a, long double b) { return a == b; }
int mc089_not_equal(long double a, long double b) { return a != b; }

int mc089_direct_greater(void) {
    long double a = 2.0L;
    long double b = 1.0L;
    return a > b;
}
