typedef unsigned char u8;

static const struct {
    u8 nName;
    char zName[7];
    float rLimit;
    float rXform;
} aXformType[] = {
    {6, "second", 4.6427e+14, 1.0},
    {6, "minute", 7.7379e+12, 60.0},
    {3, "day", 5373485.0, 86400.0},
};

static const union {
    double real;
    unsigned long long bits;
} floatingZero = {0};

static const double integerToDouble = 7;
static const float unsignedToFloat = 9u;

float static_floating_aggregate_limit(int index)
{
    return -aXformType[index].rLimit;
}

double static_floating_aggregate_scaled(double r, int index)
{
    return r * 1000.0 * aXformType[index].rXform;
}

double static_floating_overflow_return(void)
{
    return 1e5000f;
}

int static_floating_aggregate_probe(void)
{
    return aXformType[0].nName == 6 ? 0 : 1;
}
