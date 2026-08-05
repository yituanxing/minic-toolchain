int main(void)
{
    int value = 7;
    int *pointer = &value;

    *pointer = 19;
    return value;
}
