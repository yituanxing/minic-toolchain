int main(void)
{
    int value = 11;
    int *pointer = &value;
    int **chain = &pointer;
    return **chain;
}
