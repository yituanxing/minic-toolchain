int main(void)
{
    int index = 0;
    int previous = 0;
    int current = 1;
    int next = 0;

    while (index < 10) {
        next = previous + current;
        previous = current;
        current = next;
        index = index + 1;
    }
    return previous;
}
