static int pick(int *calls)
{
    *calls = *calls + 1;
    return 0;
}

int main(void)
{
    int value;
    int *pointer;
    int calls;
    unsigned mask;

    value = 85;
    pointer = &value;
    calls = 0;
    mask = 240;
    pointer[pick(&calls)] ^= 15;
    mask ^= 255;
    return value + calls * 100 + mask;
}
