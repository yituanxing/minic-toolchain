static int probe(void) {
    __attribute__((__noreturn__)) extern void __compiletime_assert_0(void)
        __attribute__((__error__("compile-time assertion failed")));

    if (0) {
        __compiletime_assert_0();
    }
    return 7;
}

int main(void) {
    return probe() - 7;
}
