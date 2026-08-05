int affine(int value);

int main(void)
{
    int base = 12;
    int result = affine(base);
    return result + base;
}

int affine(int value)
{
    int doubled = value * 2;
    return doubled + 7;
}
