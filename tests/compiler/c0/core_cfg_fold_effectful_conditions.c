/* CFG constant facts must not discard the evaluation of a condition. */
static int calls;

static int tick(void) {
    ++calls;
    return calls;
}

static void *always_null(int ignored) {
    return (void *)0;
}

int main(void) {
    if (always_null(tick()))
        return 11;
    if (calls != 1)
        return 12;

    if (!always_null(tick())) {
        if (calls != 2)
            return 13;
    } else {
        return 14;
    }

    if (((_Bool)tick()) > 1)
        return 15;
    if (calls != 3)
        return 16;

    if ((tick() != 0) < 0)
        return 17;
    if (calls != 4)
        return 18;

    if (always_null(++calls))
        return 19;
    if (calls != 5)
        return 20;

    return 0;
}
