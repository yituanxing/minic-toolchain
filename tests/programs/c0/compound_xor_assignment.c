static int pick(int *calls)
{
    *calls = *calls + 1;
    return 0;
}

int main(void)
{
    int values[1];
    int calls;
    unsigned mask;

    values[0] = 85;
    calls = 0;
    mask = 240;
    values[pick(&calls)] ^= 15;
    mask ^= 255;
    return values[0] + calls * 100 + mask;
}
