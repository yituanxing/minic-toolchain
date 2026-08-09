static inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
    __attribute__((__no_instrument_function__)) const int *
prefix_attribute_identity(const int *value)
{
    return value;
}

const int *call_prefix_attribute_identity(const int *value)
{
    return prefix_attribute_identity(value);
}
