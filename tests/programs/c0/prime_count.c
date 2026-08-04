int main(void)
{
    int value = 2;
    int divisor = 0;
    int is_prime = 0;
    int count = 0;

    while (value <= 50) {
        divisor = 2;
        is_prime = 1;
        while (divisor * divisor <= value) {
            if (value % divisor == 0)
                is_prime = 0;
            divisor = divisor + 1;
        }
        if (is_prime)
            count = count + 1;
        value = value + 1;
    }
    return count;
}
