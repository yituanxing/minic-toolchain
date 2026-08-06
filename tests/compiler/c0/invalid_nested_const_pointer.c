int inspect_values(const int **values)
{
    return 0;
}

int main(void)
{
    int value;
    int *pointer;

    value = 1;
    pointer = &value;
    return inspect_values(&pointer);
}
