struct Pair {
    int count;
    char *pointer;
};

int main(void) {
    struct Pair value = {1 - 1, ((void *)(2 - 2))};
    return value.count;
}
