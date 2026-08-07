int read_value(const int * const value);

int read_value(const int *value)
{
    return *value;
}

int main(void)
{
    int value;
    int *pointer;
    int * const fixed = &value;

    value = 7;
    *fixed = 9;
    pointer = fixed;
    return read_value(pointer);
}
