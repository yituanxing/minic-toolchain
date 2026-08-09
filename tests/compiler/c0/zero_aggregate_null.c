struct Pair {
    int count;
    char *pointer;
};

int main(void) {
    struct Pair value = {0, ((void *)0)};
    return value.count;
}
