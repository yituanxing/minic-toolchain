/* RV64 LP64D: fa0..fa7 contain only the first eight named FP arguments.
 * A ninth named FP argument is passed in the first available integer
 * register, NOT rejected or silently placed onto the stack. */
double ninth(double a0, double a1, double a2, double a3,
             double a4, double a5, double a6, double a7,
             double a8) { return a8; }

double tenth_mixed(unsigned long tag, double a0, double a1,
                   double a2, double a3, double a4, double a5,
                   double a6, double a7, double a8, double a9) {
    (void)tag;
    (void)a8;
    return a9;
}

double spill_mixed(unsigned long a, unsigned long b, unsigned long c,
                   unsigned long d, unsigned long e, unsigned long f,
                   unsigned long g, unsigned long h,
                   double a0, double a1, double a2, double a3,
                   double a4, double a5, double a6, double a7,
                   double a8) { (void)h; return a8; }

float ninth_float(double a0, double a1, double a2, double a3,
                  double a4, double a5, double a6, double a7,
                  float a8) { return a8; }

extern double (*indirect_ninth)(double, double, double, double, double,
                                double, double, double, double);

double call_ninth(double x) {
    return ninth(x,x,x,x,x,x,x,x,x);
}

double call_tenth_mixed(double x) {
    return tenth_mixed(17UL,x,x,x,x,x,x,x,x,x,x);
}

double call_spill_mixed(double x) {
    return spill_mixed(1UL,2UL,3UL,4UL,5UL,6UL,7UL,8UL,
                       x,x,x,x,x,x,x,x,x);
}

float call_ninth_float(double x, float y) {
    return ninth_float(x,x,x,x,x,x,x,x,y);
}

double call_indirect_ninth(double x) {
    return indirect_ninth(x,x,x,x,x,x,x,x,x);
}
