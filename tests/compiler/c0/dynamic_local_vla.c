static int bound_calls;

static int next_bound(void)
{
    bound_calls += 1;
    return 7;
}

int main(void)
{
    int values[next_bound()];

    values[0] = 11;
    values[6] = 13;
    return bound_calls == 1 && values[0] == 11 && values[6] == 13 ? 0 : 1;
}
