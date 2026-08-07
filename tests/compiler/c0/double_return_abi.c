double seed(void);

double relay(void)
{
    return seed();
}

double literal(void)
{
    return (double)123.5;
}

double add_values(void)
{
    return 1.5 + 2.25;
}

double subtract_values(void)
{
    return 9.0 - 2.5;
}

double multiply_values(void)
{
    return 1.5 * 4.0;
}

double divide_values(void)
{
    return 9.0 / 4.0;
}

double nan_value(void)
{
    return 0.0 / 0.0;
}

int main(void)
{
    return 0;
}
