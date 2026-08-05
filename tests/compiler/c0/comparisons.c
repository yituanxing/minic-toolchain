int main(void)
{
#if CASE == 1
    return 7 == 7;
#elif CASE == 2
    return 7 != 7;
#elif CASE == 3
    return -3 < 2;
#elif CASE == 4
    return 2 <= 2;
#elif CASE == 5
    return 9 > 4;
#elif CASE == 6
    return 4 >= 9;
#elif CASE == 7
    return 1 + 2 * 3 == 7;
#elif CASE == 8
    int x = -5;
    return x < 0;
#else
#error unsupported comparison test case
#endif
}
