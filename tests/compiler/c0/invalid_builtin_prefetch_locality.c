int main(void)
{
    int value = 0;
    __builtin_prefetch(&value, 0, 4);
    return 0;
}
