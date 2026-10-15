static int switch_tail(int value)
{
    int result = 7;

    switch (value) {
    case 1:
        break;
        result = 99;
    case 2:
        result = 42;
        break;
    default:
        result = 3;
        break;
    }
    return result;
}

int main(void)
{
    if (switch_tail(1) != 7) return 1;
    if (switch_tail(2) != 42) return 2;
    if (switch_tail(9) != 3) return 3;
    return 0;
}
