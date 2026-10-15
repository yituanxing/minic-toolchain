static int calls;

static int once(int value) {
    calls += 1;
    return value;
}

int main(void) {
    int ranged[4] = {[0 ... 3] = once(7), [2] = 9};
    int backward[3] = {[1] = 3, [0] = 4};

    return calls == 1 && ranged[0] == 7 && ranged[1] == 7 && ranged[2] == 9 && ranged[3] == 7 &&
                   backward[0] == 4 && backward[1] == 3 && backward[2] == 0
               ? 0
               : 1;
}
