/* Exercise each integer-only operator on the same pointer operand. */
int main(void)
{
    int value;
    int *pointer;

    pointer = &value;
    return pointer OP 1;
}
