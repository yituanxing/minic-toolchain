static int calls;

static int bump(void) {
    calls += 1;
    return 7;
}

int main(void) {
    int value = (bump(), 11);

    ((void)bump(), ((void)0));
    return calls == 2 && value == 11 ? 0 : 1;
}
