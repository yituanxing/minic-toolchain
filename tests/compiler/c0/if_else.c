int main(void)
{
#if CASE == 1
    int x = 0;
    if (1)
        x = 7;
    return x;
#elif CASE == 2
    int x = 0;
    if (0)
        x = 7;
    else
        x = 9;
    return x;
#elif CASE == 3
    int x = 3;
    if (x < 5) {
        x = x + 4;
    }
    return x;
#elif CASE == 4
    int x = 0;
    if (1)
        if (0)
            x = 3;
        else
            x = 8;
    return x;
#elif CASE == 5
    if (0)
        return 4;
    else
        return 6;
#elif CASE == 6
    int x = 2;
    if (0) {
        x = 9;
    }
    return x;
#elif CASE == 7
    int x = 1;
    if (1) {
        x = x + 2;
        x = x * 3;
    }
    return x;
#else
#error unsupported if/else test case
#endif
}
