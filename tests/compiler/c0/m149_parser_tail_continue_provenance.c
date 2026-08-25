static int plain_while_continue(int n) {
    int total = 0;
    while (n > 0) {
        n -= 1;
        if (n & 1)
            continue;
        total += n;
    }
    return total;
}

static int for_update_continue(int limit) {
    int i;
    int total = 0;
    for (i = 0; i < limit; i += 1) {
        if (i & 1)
            continue;
        total += i;
    }
    return total;
}

static int for_unbounded_continue(int limit) {
    int i = 0;
    for (;;) {
        i += 1;
        if (i < limit)
            continue;
        return i;
    }
}

int main(void) {
    return plain_while_continue(5) == 6 &&
           for_update_continue(6) == 6 &&
           for_unbounded_continue(4) == 4 ? 0 : 1;
}
