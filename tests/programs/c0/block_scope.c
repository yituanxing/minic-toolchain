int main(void)
{
    int value = 2;
    int result = 0;

    if (1) {
        int value = 7;
        result = value * 10;

        if (1) {
            int value = 3;
            result = result + value;
        }

        result = result + value;
    }

    return result + value;
}
