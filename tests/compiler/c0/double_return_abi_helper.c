#include <stdlib.h>

double relay(void);
double literal(void);
double add_values(void);
double subtract_values(void);
double multiply_values(void);
double divide_values(void);
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
    if (add_values() != 3.75)
    {
        _Exit(75);
    }
    if (subtract_values() != 6.5)
    {
        _Exit(76);
    }
    if (multiply_values() != 6.0)
    {
        _Exit(77);
    }
    if (divide_values() != 2.25)
    {
        _Exit(78);
    }
    value = nan_value();
    if (value == value)
    {
        _Exit(79);
    }
}
