int helper(void)
{
    return 3;
}

int middle(void)
{
    return helper() + 4;
}

int self(void)
{
    if (0) {
        return self();
    }
    return 7;
}

int main(void)
{
    int x = 5;
    return x + middle() + self();
}
