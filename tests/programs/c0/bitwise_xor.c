int main(void)
{
    int precedence;
    int signed_value;
    unsigned high;
    unsigned mixed;

    precedence = 4 ^ 1 == 1;
    signed_value = -7 ^ 3;
    high = 0 - 1;
    mixed = high ^ 255;
    return precedence + (mixed % 251) + (signed_value + 10);
}
