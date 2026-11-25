#include <stdlib.h>

double relay(void);
double literal(void);
double add_values(double, double);
double subtract_values(double, double);
double multiply_values(double, double);
double divide_values(double, double);
double nan_value(void);

double seed(void)
{
    return 123.5;
}

__attribute__((constructor)) static void validate_double_return_abi(void)
{
    double value;

    if (relay() != 123.5)
    {
        _Exit(73);
    }
    if (literal() != 123.5)
    {
        _Exit(74);
    }
    if (add_values(1.5, 2.25) != 3.75)
    {
        _Exit(75);
    }
    if (subtract_values(9.0, 2.5) != 6.5)
    {
        _Exit(76);
    }
    if (multiply_values(1.5, 4.0) != 6.0)
    {
        _Exit(77);
    }
    if (divide_values(9.0, 4.0) != 2.25)
    {
        _Exit(78);
    }
    value = nan_value();
    if (value == value)
    {
        _Exit(79);
    }
}
