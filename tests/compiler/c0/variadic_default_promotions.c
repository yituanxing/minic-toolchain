extern int variadic_sink(const char *format, ...);

int pass_variadic_float(float value)
{
    return variadic_sink("%f", value);
}

int pass_variadic_char(signed char value)
{
    return variadic_sink("%d", value);
}
