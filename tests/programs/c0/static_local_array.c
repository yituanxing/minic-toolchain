static int counter(void)
{
    static int slots[1];

    slots[0] = slots[0] + 1;
    return slots[0];
}

static int other(void)
{
    static int slots[1];

    slots[0] = slots[0] + 5;
    return slots[0];
}

static int char_slot(void)
{
    static char version[15];

    version[0] = 7;
    return version[0];
}

int main(void)
{
    if (counter() != 1)
    {
        return 1;
    }
    if (counter() != 2)
    {
        return 2;
    }
    if (other() != 5)
    {
        return 3;
    }
    if (counter() != 3)
    {
        return 4;
    }
    if (char_slot() != 7)
    {
        return 5;
    }
    return 0;
}
