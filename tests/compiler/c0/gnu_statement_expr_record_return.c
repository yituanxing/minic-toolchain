/* A record-valued GNU statement expression may be a function's return value.
 * Its side effects must run once, and its final aggregate may be a call. */
typedef struct Pair {
    long first;
    long second;
} Pair;

static int effects;

static Pair make_pair(long input) {
    Pair result;
    result.first = input;
    result.second = input + 7;
    return result;
}

static Pair make_from_statement(long input) {
    return ({
        long adjusted = input + 3;
        ++effects;
        ({ int unused = effects; (void)unused; });
        make_pair(adjusted);
    });
}

int main(void) {
    Pair pair = make_from_statement(23);
    if (pair.first != 26 || pair.second != 33 || effects != 1)
        return 1;
    pair = make_from_statement(8);
    if (pair.first != 11 || pair.second != 18 || effects != 2)
        return 2;
    return 0;
}
