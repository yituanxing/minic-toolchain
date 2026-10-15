static int call_block_declared(void)
{
    int block_helper(int value);
    return block_helper(41);
}

int block_helper(int value)
{
    return value + 1;
}

int main(void)
{
    return call_block_declared() == 42 ? 0 : 1;
}
