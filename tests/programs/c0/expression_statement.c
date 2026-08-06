static void set_value(int *value, int replacement)
{
    *value = replacement;
}

static int increment(int *value)
{
    *value = *value + 1;
    return *value;
}

int main(void)
{
    int value;

    value = 4;
    set_value(&value, 9);
    increment(&value);
    value + 100;
    return value;
}
