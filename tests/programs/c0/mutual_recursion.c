int is_even(int value);
int is_odd(int value);

int is_even(int value)
{
    if (value == 0)
        return 1;
    return is_odd(value - 1);
}

int is_odd(int value)
{
    if (value == 0)
        return 0;
    return is_even(value - 1);
}

int main(void)
{
    return is_even(8) * 100 + is_odd(7) * 20 + is_even(6);
}
