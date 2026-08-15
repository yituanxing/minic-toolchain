int main(void)
{
    int value = 0;
    __builtin_prefetch(&value, 3);
    return 0;
}
