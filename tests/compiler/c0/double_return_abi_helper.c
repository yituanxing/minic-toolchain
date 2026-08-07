#include <stdlib.h>

double relay(void);
double literal(void);

double seed(void)
{
    return 123.5;
}

__attribute__((constructor)) static void validate_double_return_abi(void)
{
    if (relay() != 123.5)
    {
        _Exit(73);
    }
    if (literal() != 123.5)
    {
        _Exit(74);
    }
}
