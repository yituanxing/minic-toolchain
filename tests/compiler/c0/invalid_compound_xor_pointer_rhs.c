int main(void)
{
    int value;
    int *pointer;

    pointer = &value;
    value ^= pointer;
    return 0;
}
