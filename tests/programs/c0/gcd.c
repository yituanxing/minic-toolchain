int main(void)
{
    int a = 1071;
    int b = 462;
    int remainder = 0;

    while (b != 0) {
        remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}
