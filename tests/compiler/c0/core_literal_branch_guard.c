/* Genuine GNU Linux typecheck idiom: the statement-expression's last
 * integer literal is the tested value, even though it owns local objects.
 * C-level side effects before a literal condition must still execute. */
static volatile unsigned long observed;

int literal_guard(unsigned short value) {
    do {
        extern void __compiletime_assert_typecheck(void)
            __attribute__((error("typecheck is a compile-time fact")));
        if (!(!(!({ unsigned short a; typeof(value) b;
                    (void)(&a == &b); 1; }))))
            __compiletime_assert_typecheck();
    } while (0);
    if ((observed += value, 1))
        return (int)observed + 3;
    return -99;
}

int literal_false(unsigned short value) {
    if ((observed += value, 0))
        return -99;
    return (int)observed;
}

int nonliteral_branch(int value) {
    return value < 0 ? 37 : 43;
}

int main(void) {
    return literal_guard(7) != 10 || literal_false(11) != 18 ||
           nonliteral_branch(-3) != 37 || nonliteral_branch(9) != 43;
}
