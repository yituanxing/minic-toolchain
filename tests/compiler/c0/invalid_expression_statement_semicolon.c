static void touch(int *value)
{
    *value = 1;
}

int main(void)
{
    int value;

    touch(&value)
    return value;
}
