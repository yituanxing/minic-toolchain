static int for_unbounded_continue(int limit) {
    int i = 0;
    for (;;) {
        i += 1;
        if (i < limit)
            continue;
        return i;
    }
}

static int for_update_continue(int limit) {
    int i = 0;
    int sum = 0;
    for (; i < limit; i += 1) {
        if (i & 1)
            continue;
        sum += i;
    }
    return sum;
}

static int nested_for_continue(int limit) {
    int outer = 0;
    int total = 0;
    for (; outer < limit; outer += 1) {
        int inner = 0;
        for (;;) {
            inner += 1;
            if (inner < 2)
                continue;
            break;
        }
        total += inner;
    }
    return total;
}

static int ordinary_for_unchanged(int limit) {
    int i;
    int sum = 0;
    for (i = 0; i < limit; i += 1)
        sum += i;
    return sum;
}

int main(void) {
    return for_unbounded_continue(4) == 4 &&
           for_update_continue(6) == 6 &&
           nested_for_continue(3) == 6 &&
           ordinary_for_unchanged(5) == 10 ? 0 : 1;
}
