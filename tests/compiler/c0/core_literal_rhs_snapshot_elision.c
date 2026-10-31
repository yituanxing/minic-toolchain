/* A literal RHS cannot invalidate the already-evaluated Core SSA value of
 * the LHS. Dynamic RHS expressions must retain their existing snapshot path.
 * Test source-level effects and cross-width conversions against GNU -O2. */
static volatile int minic_literal_reads;
static volatile int minic_dynamic_left;
static volatile int minic_dynamic_right;

static int left_value(void)
{
    ++minic_dynamic_left;
    return 17;
}

static int right_value(void)
{
    ++minic_dynamic_right;
    return 4;
}

static long long literal_chain(long long value)
{
    return ((value + 5LL) ^ 3LL) - 12LL;
}

int main(void)
{
    int value = 0;
    int dynamic_result;
    void *ptr = &value;
    minic_literal_reads = 9;
    if ((minic_literal_reads + 7) != 16)
        return 1;
    if (minic_literal_reads != 9)
        return 2;
    if (literal_chain(10LL) != 0LL)
        return 3;
    if ((0x5au & 15U) != 10U || (0x5au | 5U) != 95U)
        return 4;
    if (!(ptr != 0) || ptr == 0)
        return 5;
    if (((unsigned long long)1 << 33) + 7ULL != 8589934599ULL)
        return 6;
    dynamic_result = left_value() + right_value();
    if (dynamic_result != 21 ||
        minic_dynamic_left != 1 || minic_dynamic_right != 1)
        return 7;
    return 0;
}
