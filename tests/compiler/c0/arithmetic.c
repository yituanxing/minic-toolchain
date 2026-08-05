int main(void)
{
#if CASE == 1
    return 1 + 2 * 3;
#elif CASE == 2
    return (8 - 3) * 2;
#elif CASE == 3
    return 20 / 3 + 20 % 3;
#elif CASE == 4
    return -1 + 10;
#else
#error unsupported arithmetic test case
#endif
}
