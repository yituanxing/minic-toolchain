typedef struct {
    double rSum;
    double rErr;
} VolatileSumCtx;

static void volatile_parameter_step(volatile VolatileSumCtx *pSum,
                                    volatile double r)
{
    volatile double s = pSum->rSum;
    volatile double t = s + r;

    pSum->rErr += (s - t) + r;
    pSum->rSum = t;
}

double volatile_parameter_probe(double a, double b)
{
    VolatileSumCtx sum = {a, 0.0};
    volatile_parameter_step(&sum, b);
    return sum.rSum + sum.rErr;
}
