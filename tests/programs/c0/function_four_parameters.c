int one(void)
{
    return 1;
}

int two(void)
{
    return 2;
}

int three(void)
{
    return 3;
}

int four(void)
{
    return 4;
}

int echo(int value)
{
    return value;
}

int mix(int value, int second, int third, int fourth)
{
    return value * 40 + second * 10 + third * 2 + fourth;
}

int main(void)
{
    int saved = 5;
    return mix(echo(one()), two(), three(), four()) + saved;
}
