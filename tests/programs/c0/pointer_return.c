static int *identity(int *pointer)
{
    return pointer;
}

int main(void)
{
    int value;
    int *result;

    value = 29;
    result = identity(&value);
    return *result;
}
