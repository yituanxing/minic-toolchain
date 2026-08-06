int write_value(int *value)
{
    *value = 2;
    return *value;
}

int main(void)
{
    const int value = 1;

    return write_value(&value);
}
