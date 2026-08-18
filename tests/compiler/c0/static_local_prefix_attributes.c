struct key {
    long value;
};
static long run(void) {
    static __attribute__((__unused__)) struct key key_value;
    static __attribute__((__unused__)) long scalar;
    return key_value.value + scalar;
}
int main(void) {
    return (int)run();
}
