static const int global_values[2] = {31, 0};

int read_first(const int *pointer)
{
    return pointer[0];
}

int bump_first(int *pointer)
{
    pointer[0] = pointer[0] + 1;
    return pointer[0];
}

int local_case(void)
{
    int values[2];

    values[0] = 10;
    return bump_first(values);
}

int static_case(void)
{
    static int values[2];

    values[0] = 20;
    return bump_first(values);
}

int global_case(void)
{
    return read_first(global_values);
}

int main(void)
{
    return local_case() + static_case() + global_case() - 63;
}
