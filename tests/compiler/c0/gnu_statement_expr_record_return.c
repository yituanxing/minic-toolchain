/* A record-valued GNU statement expression may be a function's return value.
 * Its side effects must run once, and its final aggregate may be a call. */
typedef struct Pair {
    long first;
    long second;
} Pair;

/* Fixed-width records cover the 8-byte register and 32-byte indirect-result
 * paths on both ILP32 and LP64 targets, in addition to Pair's 16/8 bytes. */
typedef struct Small {
    unsigned long long value;
} Small;
typedef struct Large {
    unsigned long long a, b, c, d;
} Large;

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

static Small make_small(unsigned long long input) {
    Small result = { input + 7ULL };
    return result;
}

static Large make_large(unsigned long long input) {
    Large result = { input, input + 1ULL, input + 2ULL, input + 3ULL };
    return result;
}

static Small small_from_statement(unsigned long long input) {
    return ({
        unsigned long long adjusted = input + 2ULL;
        ({ int positive = adjusted != 0ULL; if (positive) effects += 3; positive; });
        make_small(adjusted);
    });
}

static Large large_from_statement(unsigned long long input) {
    return ({
        unsigned long long adjusted = input + 4ULL;
        ({ int positive = adjusted != 0ULL; if (positive) effects += 5; positive; });
        make_large(adjusted);
    });
}

int main(void) {
    Pair pair = make_from_statement(23);
    if (pair.first != 26 || pair.second != 33 || effects != 1)
        return 1;
    pair = make_from_statement(8);
    if (pair.first != 11 || pair.second != 18 || effects != 2)
        return 2;
    {
        Small small = small_from_statement(5ULL);
        Large large = large_from_statement(9ULL);
        if (small.value != 14ULL || large.a != 13ULL || large.b != 14ULL ||
            large.c != 15ULL || large.d != 16ULL || effects != 10)
            return 3;
    }
    return 0;
}
