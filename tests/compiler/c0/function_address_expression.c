typedef int (*MiniUnary)(int value);

static int increment(int value) {
    return value + 1;
}

MiniUnary direct_function_address(void) {
    return &increment;
}

MiniUnary cancel_function_dereference(MiniUnary function) {
    return &*function;
}
