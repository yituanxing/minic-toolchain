long rv_leaf_mul(long a, long b) { return a * b; }
long rv_leaf_div(long a, long b) { return a / b; }
unsigned long rv_leaf_rem(unsigned long a, unsigned long b) {
    return a % b;
}
double rv_leaf_double_add(double a, double b) { return a + b; }
int rv_leaf_double_cmp(double a, double b) { return a < b; }
double rv_leaf_itod(long a) { return (double)a; }
long rv_leaf_dtoi(double a) { return (long)a; }
double rv_leaf_ftod(float a) { return (double)a; }
float rv_leaf_dtof(double a) { return (float)a; }
