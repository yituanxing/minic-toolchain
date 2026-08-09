static int values[5];
static int calls;

static int **next_slot(void) {
    static int *pointer;
    calls += 1;
    return &pointer;
}

int main(void) {
    int *pointer = values + 4;

    pointer -= 2;
    *next_slot() = values + 4;
    *next_slot() -= 3;
    return pointer == values + 2 && **next_slot() == values + 1 && calls == 3 ? 0 : 1;
}
