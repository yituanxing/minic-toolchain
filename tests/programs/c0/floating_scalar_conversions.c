int main(void)
{
    double value = 7.75;
    int result = 0;

    if (value > 7)
    {
        result = result + 1;
    }
    if (value >= 7)
    {
        result = result + 2;
    }
    if (value < 8)
    {
        result = result + 4;
    }
    if (value <= 8)
    {
        result = result + 8;
    }
    if (value != 7)
    {
        result = result + 16;
    }
    if (value == 7.75)
    {
        result = result + 32;
    }
    if ((int)value == 7)
    {
        result = result + 64;
    }

    if (result == 127)
    {
        return 0;
    }
    return 1;
}
