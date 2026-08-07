static int *make_null(void)
{
    return (void *)0;
}

int main(void)
{
    int value;
    int *pointer;
    void *untyped;

    untyped = (void *)0;
    pointer = untyped;
    pointer = make_null();

    value = 31;
    pointer = &value;
    return *pointer;
}
