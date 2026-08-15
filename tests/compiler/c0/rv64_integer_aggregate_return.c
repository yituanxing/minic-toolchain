struct pair64 {
    long first;
    long second;
};

static struct pair64 add_pair(struct pair64 lhs, struct pair64 rhs) {
    struct pair64 result;

    result.first = lhs.first + rhs.first;
    result.second = lhs.second + rhs.second;
    return result;
}

static struct pair64 forward_pair(struct pair64 lhs, struct pair64 rhs) {
    return add_pair(lhs, rhs);
}

int main(void) {
    return 0;
}

struct triple64 {
    long first;
    long second;
    long third;
};

static struct triple64 make_triple(long base) {
    struct triple64 result;

    result.first = base;
    result.second = base + 1;
    result.third = base + 2;
    return result;
}

static struct triple64 forward_triple(long base) {
    return make_triple(base);
}

static void cleanup_triple(struct triple64 *value) {
    value->third += 1;
}

static long cleanup_triple_call(long base) {
    struct triple64 value __attribute__((cleanup(cleanup_triple))) = make_triple(base);

    return value.first + value.second + value.third;
}
