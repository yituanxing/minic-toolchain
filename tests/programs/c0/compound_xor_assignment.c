static int *pick(int *value, int *calls)
{
    *calls = *calls + 1;
    return value;
}

int main(void)
{
    int value;
    int calls;
    unsigned mask;

    value = 85;
    calls = 0;
    mask = 240;
    *pick(&value, &calls) ^= 15;
    mask ^= 255;
    return value + calls * 100 + mask;
}
