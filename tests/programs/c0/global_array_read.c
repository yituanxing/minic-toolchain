static const int table[4] = {3, 5, 11, 17};

int read_table(int index)
{
    return table[index];
}

int main(void)
{
    int table;

    table = 4;
    return read_table(2) + table;
}
