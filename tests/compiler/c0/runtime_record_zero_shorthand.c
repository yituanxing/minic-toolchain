struct inner {
    long left;
    long right;
};
struct outer {
    struct inner nested;
    long tail;
};
static long run(void) {
    struct outer first = {0};
    struct outer second = {
        0,
    };
    return first.nested.left + first.nested.right + first.tail + second.nested.left +
           second.nested.right + second.tail;
}
int main(void) {
    return (int)run();
}
