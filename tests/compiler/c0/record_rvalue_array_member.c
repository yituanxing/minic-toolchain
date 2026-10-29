/* An array selected from an aggregate rvalue is not an lvalue, but may
 * decay into a pointer to an array inside the full-expression temporary. */
struct Inner {
    long words[3];
};

struct Outer {
    struct Inner inner;
    long tail;
};

static int evaluations;

static struct Outer produce(long x) {
    struct Outer result;
    evaluations++;
    result.inner.words[0] = x;
    result.inner.words[1] = x + 1;
    result.inner.words[2] = x + 2;
    result.tail = x + 3;
    return result;
}

static long consume(const long *words) {
    return words[0] + words[2];
}

int main(void) {
    long first = produce(10).inner.words[1];
    long second = consume(produce(20).inner.words);
    long third = (0 ? produce(30) : produce(40)).inner.words[2];
    unsigned long extent = sizeof(produce(50).inner.words);
    if (first != 11 || second != 42 || third != 42 ||
        extent != 3 * sizeof(long) || evaluations != 3)
        return 1;
    return 0;
}
