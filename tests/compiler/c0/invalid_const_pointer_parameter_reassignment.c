int replace(int * const value, int *replacement)
{
    value = replacement;
    return 0;
}

int main(void)
{
    int first;
    int second;

    return replace(&first, &second);
}
