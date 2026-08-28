static int update_calls;

static int next_i(int i) {
    update_calls += 1;
    return i + 1;
}

static int update_continue(int limit) {
    int i;
    int sum = 0;
    update_calls = 0;
    for (i = 0; i < limit; i = next_i(i)) {
        if (i & 1)
            continue;
        sum += i;
    }
    return sum + update_calls * 100;
}

static int no_update_continue(int limit) {
    int i = 0;
    for (;;) {
        i += 1;
        if (i < limit)
            continue;
        return i;
    }
}

int main(void) {
    return update_continue(5) == 506 && no_update_continue(4) == 4 ? 0 : 1;
}
