int main(void)
{
#if CASE == 1
    int x = 0;
    while (x < 5)
        x = x + 1;
    return x;
#elif CASE == 2
    int i = 1;
    int sum = 0;
    while (i <= 5) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
#elif CASE == 3
    int x = 7;
    while (0)
        x = 9;
    return x;
#elif CASE == 4
    int x = 0;
    int y = 0;
    int count = 0;
    while (x < 3) {
        y = 0;
        while (y < 2) {
            count = count + 1;
            y = y + 1;
        }
        x = x + 1;
    }
    return count;
#elif CASE == 5
    int x = 0;
    while (1) {
        x = x + 1;
        if (x == 4)
            return x;
    }
#else
#error unsupported while test case
#endif
}
