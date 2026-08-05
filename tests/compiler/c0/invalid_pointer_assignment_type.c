int main(void)
{
    int value = 1;
    int *pointer = &value;

    *pointer = pointer;
    return value;
}
