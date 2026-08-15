int main(void)
{
    int values[4];
    int *cursor = values;

    __builtin_prefetch(cursor);
    __builtin_prefetch(cursor++, 1);
    __builtin_prefetch(cursor++, 2, 0);
    return cursor == values + 2 ? 0 : 1;
}
