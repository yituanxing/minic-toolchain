static int trace_value;

static void mark_return_expression(void)
{
    trace_value = trace_value * 10 + 1;
}

static void mark_cleanup(int *value)
{
    (void)value;
    trace_value = trace_value * 10 + 2;
}

static void return_void_call(void)
{
    int guard __attribute__((__cleanup__(mark_cleanup))) = 0;
    return mark_return_expression();
}

static void return_void_cast(int value)
{
    return (void)value;
}

int main(void)
{
    return_void_call();
    return_void_cast(7);
    return trace_value == 12 ? 0 : 1;
}
