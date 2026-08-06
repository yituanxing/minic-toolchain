int preserve_value(int input)
{
    const int saved = input + 5;
    return saved * 2;
}

int main(void)
{
    return preserve_value(11);
}
