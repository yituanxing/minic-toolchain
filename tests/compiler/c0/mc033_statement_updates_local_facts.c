/* MC-033: an effect-only update must invalidate the Core local constant fact. */
static int original_counterexample(void) {
    int x = 0;
    x++;
    if (x == 0) return 99;
    return 7;
}

static int statement_post_increment(void) {
    int x = 0;
    x++;
    if (x == 0) return 11;
    return x == 1 ? 0 : 12;
}

static int statement_post_decrement(void) {
    int x = 0;
    x--;
    if (x == 0) return 21;
    return x == -1 ? 0 : 22;
}

static int statement_pre_increment(void) {
    int x = 0;
    ++x;
    if (x == 0) return 31;
    return x == 1 ? 0 : 32;
}

static int statement_pre_decrement(void) {
    int x = 0;
    --x;
    if (x == 0) return 41;
    return x == -1 ? 0 : 42;
}

static int nested_update_value(void) {
    int x = 0;
    int old = x++;
    int updated = ++x;
    if (old != 0 || updated != 2 || x != 2) return 51;
    if (x++ == 2) {
        if (x == 2) return 52;
    } else {
        return 53;
    }
    return x == 3 ? 0 : 54;
}

static int goto_reentry(void) {
    int count = 0;
again:
    count++;
    if (count < 3) goto again;
    if (count == 0) return 61;
    return count == 3 ? 0 : 62;
}

static int continue_reentry(void) {
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        count++;
        if (i < 2) continue;
    }
    return count == 3 ? 0 : 71;
}

int main(void) {
    int code;
    if (original_counterexample() != 7) return 81;
    if ((code = statement_post_increment()) != 0) return code;
    if ((code = statement_post_decrement()) != 0) return code;
    if ((code = statement_pre_increment()) != 0) return code;
    if ((code = statement_pre_decrement()) != 0) return code;
    if ((code = nested_update_value()) != 0) return code;
    if ((code = goto_reentry()) != 0) return code;
    if ((code = continue_reentry()) != 0) return code;
    return 0;
}
