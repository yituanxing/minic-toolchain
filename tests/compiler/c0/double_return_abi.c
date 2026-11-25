double seed(void);

double relay(void)
{
    return seed();
}

double literal(void)
{
    return (double)123.5;
}

/* The independent GNU caller supplies run-time operands from another TU:
 * no source-level constant folding may replace the actual binary64 ops. */
double add_values(double left, double right)
{
    return left + right;
}

double subtract_values(double left, double right)
{
    return left - right;
}

double multiply_values(double left, double right)
{
    return left * right;
}

double divide_values(double left, double right)
{
    return left / right;
}

double nan_value(void)
{
    return 0.0 / 0.0;
}

int main(void)
{
    return 0;
}
