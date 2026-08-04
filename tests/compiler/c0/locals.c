int main(void)
{
#if CASE == 1
    int x = 7;
    return x;
#elif CASE == 2
    int x;
    int y = 4;
    x = 7;
    return x + y;
#elif CASE == 3
    int x = 3;
    x = x * 5;
    return x;
#else
#error unsupported local test case
#endif
}
