struct pair {
    int left;
    int right;
};

static const struct pair bad[3] = {
    [0 ... 1] = {1, 2},
};
