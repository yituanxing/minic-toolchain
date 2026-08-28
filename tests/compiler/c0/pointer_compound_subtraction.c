struct Pair {
    int left;
    int right;
};

int read_adjusted(struct Pair *pair, int count) {
    pair += count;
    pair -= 1;
    return pair->right;
}

int pointer_step(int value);

char *read_cfg_adjusted(char *pointer, int condition) {
    pointer += pointer_step(condition ? 1 : 2);
    return pointer;
}
