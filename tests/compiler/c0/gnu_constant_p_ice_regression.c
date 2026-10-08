/* Regression for local-facts constant_p queries used inside GCC ICEs.
 * A runtime-dependent expression is not itself constant, but
 * __builtin_constant_p(runtime_expression) is an ICE whose value is zero.
 */
struct interval {
    int cur;
    int seg;
};

_Static_assert(__builtin_constant_p(6 * 7), "known integer constant");
_Static_assert(
    __builtin_choose_expr(__builtin_constant_p(6 * 7), 6 * 7 == 42, 0),
    "known constant_p must choose the true arm");

static int check_segment(struct interval *p) {
    _Static_assert(
        __builtin_choose_expr(
            __builtin_constant_p(p->cur < p->seg),
            p->cur < p->seg,
            1),
        "dynamic local must select the fallback arm");
    _Static_assert(
        __builtin_choose_expr(
            __builtin_constant_p(p->cur < p->seg),
            0,
            __builtin_choose_expr(__builtin_constant_p(3 + 4), 7 == 7, 0)),
        "nested compile-time selection");
    return p->cur < p->seg;
}

int main(void) {
    struct interval one;
    one.cur = 3;
    one.seg = 7;
    if (check_segment(&one) != 1)
        return 1;
    one.cur = 9;
    if (check_segment(&one) != 0)
        return 2;
    return 0;
}
