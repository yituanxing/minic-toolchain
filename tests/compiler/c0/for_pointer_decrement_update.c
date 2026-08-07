int main(void)
{
    int value;
    int *pointer;

    pointer = &value;
    for (pointer = &value; ; --pointer) {
        break;
    }
    return 0;
}
