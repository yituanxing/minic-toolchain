int main(void)
{
    unsigned i;

    for (i = 0; i < 4; i = i + 1) {
        if (i == 3) {
            return 0;
        }
    }
    return 1;
}
