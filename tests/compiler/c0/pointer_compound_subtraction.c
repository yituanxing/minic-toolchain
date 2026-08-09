struct Pair {
    int left;
    int right;
};

int read_adjusted(struct Pair *pair, int count) {
    pair += count;
    pair -= 1;
    return pair->right;
}
