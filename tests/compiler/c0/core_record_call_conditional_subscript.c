/* Regression for CSR machine source select: a record-returning RHS call
 * must stay single-evaluation when the LHS ternary subscript splits Core CFG.
 * A 24-byte result exercises the by-value aggregate storage path on LP64. */
struct Record {
    unsigned long first;
    unsigned long second;
    unsigned long third;
};

static struct Record bindings[3];
static unsigned int calls;

static struct Record make_record(unsigned long value) {
    struct Record result;
    ++calls;
    result.first = value;
    result.second = value + 1UL;
    result.third = value + 2UL;
    return result;
}

static int check(unsigned int form, unsigned long input) {
    unsigned int destination = form == 1U ? 1U : 2U;
    unsigned int other = destination == 1U ? 2U : 1U;
    unsigned int before = calls;

    bindings[other].first = 0UL;
    bindings[form == 1U ? 1U : 2U] = make_record(input);
    return calls == before + 1U &&
           bindings[destination].first == input &&
           bindings[destination].second == input + 1UL &&
           bindings[destination].third == input + 2UL &&
           bindings[other].first == 0UL;
}

int main(void) {
    if (!check(1U, 17UL)) return 1;
    if (!check(2U, 31UL)) return 2;
    return calls == 2U ? 0 : 3;
}
