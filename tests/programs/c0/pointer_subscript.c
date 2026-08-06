int read_at(int *values, int index)
{
    return values[index];
}

int main(void)
{
    int values[4];
    int *pointer;

    pointer = &values[0];
    pointer[0] = 5;
    pointer[1] = 17;
    pointer[2] = pointer[0] + pointer[1];
    return read_at(pointer, 2);
}
