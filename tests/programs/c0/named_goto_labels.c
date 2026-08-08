int main(void)
{
    int value = 0;

    goto forward_target;
    value = 99;

forward_target:
    value = value + 1;

loop_target:
    value = value + 1;
    if (value < 4) {
        goto loop_target;
    }

    if (value != 4) {
        return 1;
    }
    return 0;
}
