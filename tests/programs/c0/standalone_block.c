int main(void)
{
    int value = 5;
    int result = 0;

    {
        int value = 9;
        result = value;

        {
            int value = 4;
            result = result + value;
        }

        result = result + value;
    }

    return result * 10 + value;
}
