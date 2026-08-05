int main(void)
{
    int value = 5;
    int other = 29;
    int *pointer = &value;
    int **chain = &pointer;

    **chain = 23;
    *chain = &other;
    return value + *pointer;
}
