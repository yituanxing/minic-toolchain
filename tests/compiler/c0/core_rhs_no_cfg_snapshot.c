/* A non-branching RHS can reuse an already evaluated Core SSA LHS without
 * creating an extra temporary object. CFG-producing RHS forms still need
 * their historical cross-block snapshot. Check both paths against GNU C. */
static volatile int lhs_reads;
static volatile int rhs_reads;
static volatile int branch_effects;

__attribute__((noinline)) static int read_left(void)
{
    ++lhs_reads;
    return 11;
}

__attribute__((noinline)) static int read_right(void)
{
    ++rhs_reads;
    return 7;
}

static int straightline_rhs(int value, int index, int *array)
{
    return read_left() + ((array[index] ^ value) & 63);
}

static int conditional_rhs(int value, int choose)
{
    return value + (choose ? (++branch_effects + 3) : (branch_effects + 5));
}

static int shortcircuit_rhs(int value, int condition)
{
    return value + (condition && ++branch_effects);
}

static int statement_rhs(int value)
{
    return value + ({ int temporary = 6; temporary + 3; });
}

static int call_rhs(void)
{
    return read_left() + read_right();
}

int main(void)
{
    int values[3] = {5, 9, 12};
    if (straightline_rhs(3, 2, values) != 26 || lhs_reads != 1)
        return 1;
    if (conditional_rhs(10, 1) != 14 || branch_effects != 1)
        return 2;
    if (conditional_rhs(10, 0) != 16 || branch_effects != 1)
        return 3;
    if (shortcircuit_rhs(10, 0) != 10 || branch_effects != 1)
        return 4;
    if (shortcircuit_rhs(10, 1) != 11 || branch_effects != 2)
        return 5;
    if (statement_rhs(10) != 19)
        return 6;
    if (call_rhs() != 18 || lhs_reads != 2 || rhs_reads != 1)
        return 7;
    return 0;
}
