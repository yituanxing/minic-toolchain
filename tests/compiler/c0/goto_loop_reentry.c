static int reentry_probe(int value)
{
    int total = 0;

    goto jump_in;
    while (value < 3) {
        total += 10;
jump_in:
        total += 1;
        value += 1;
    }
    return total;
}

int main(void)
{
    return reentry_probe(0) == 23 ? 0 : 1;
}
