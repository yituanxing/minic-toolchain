/* GNU choose condition must be an ICE, not an arbitrary parameter. */
int invalid_choose(int flag) {
    return __builtin_choose_expr(flag, 1, 2);
}
