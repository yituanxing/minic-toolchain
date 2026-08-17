static int increment(int value) {
    return value + 1;
}
static int apply(int callback(int), int value) {
    return callback(value);
}
int main(void) {
    return apply(increment, 6) == 7 ? 0 : 1;
}
