/* Native floating operations, scalar ABI arguments/returns, comparisons,
 * casts, and stack-passed mixed-bank parameters (soft FP for RV32I/RV64I). */
volatile double mir_fp_sink;

__attribute__((noinline))
double mir_fp_callee(double a, double b, double c, double d,
                     double e, double f, double g, double h,
                     double i, double j) {
    double left = (a + b) * (c - d);
    float narrow = (float)(e + g);
    double right = (double)narrow / (f + 1.0);
    return left + right + h - i + j;
}

__attribute__((noinline))
long mir_fp_bridge(long seed, float scale, double bias) {
    double a = (double)seed + (double)scale;
    double b = mir_fp_callee(a, bias, 1.5, 2.0, 3.5,
                             4.0, 5.5, 6.0, 7.5, 8.0);
    mir_fp_sink = b;
    return (long)(b >= 0.0 ? b : -b);
}

int main(void) { return (int)(mir_fp_bridge(13, 2.5f, 0.25) & 255L); }
