int main(void)
{
    int first;
    int second;
    int * const fixed = &first;

    fixed = &second;
    return 0;
}
