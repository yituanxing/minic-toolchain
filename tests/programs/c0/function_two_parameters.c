int source(void)
{
    return 4;
}

int adjust(void)
{
    return 3;
}

int combine(int left, int right)
{
    return left * 10 + right;
}

int main(void)
{
    int saved = 5;
    return combine(source(), adjust()) + saved;
}
