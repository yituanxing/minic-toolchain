struct pair {
    long left;
    long right;
};
static void fill(long value) {
    struct pair values[2] = {
        {.left = value, .right = 2},
        {.left = 3, .right = value},
    };
    (void)values;
}
int main(void) {
    fill(1);
    return 0;
}
