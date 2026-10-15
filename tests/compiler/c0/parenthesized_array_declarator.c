static const char *(parenthesized_array[]) = {
    "alpha",
    "beta",
    "gamma"
};

const char *parenthesized_array_pick(int index)
{
    return parenthesized_array[index];
}
