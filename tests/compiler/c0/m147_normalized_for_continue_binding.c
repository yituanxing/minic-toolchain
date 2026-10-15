static int plain_while_baseline(int n) {
    int sum = 0;
    while (n > 0) {
        n -= 1;
        if (n & 1)
            sum += n;
    }
    return sum;
}

static int normalized_for_continue(int n) {
    int i;
    int sum = 0;
    for (i = 0; i < n; i += 1) {
        if (i & 1)
            continue;
        sum += i;
    }
    return sum;
}

int main(void) {
    return plain_while_baseline(5) == 4 &&
           normalized_for_continue(6) == 6 ? 0 : 1;
}
