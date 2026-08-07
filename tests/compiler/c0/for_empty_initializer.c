int main(void)
{
    unsigned i;

    i = 0;
    for (; i < 4; ++i) {
        if (i == 3) {
            return 0;
        }
    }
    return 1;
}
