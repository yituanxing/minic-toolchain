/* A boolean-range result says nothing about whether its operand ran. */
static int calls;

static int tick(void) {
    ++calls;
    return calls;
}

int main(void) {
    if (((_Bool)tick()) > 1)
        return 11;
    if (calls != 1)
        return 12;

    if ((tick() != 0) < 0)
        return 13;
    if (calls != 2)
        return 14;

    return 0;
}
