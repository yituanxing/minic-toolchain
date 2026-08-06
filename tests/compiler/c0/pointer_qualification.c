int read_value(const int *value)
{
    return *value;
}

int main(void)
{
    int value;

    value = 9;
    return read_value(&value);
}
