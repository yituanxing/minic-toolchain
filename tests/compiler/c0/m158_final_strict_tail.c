extern void m158_sink(void);

int m158_ctzl(unsigned long x) {
    return __builtin_ctzl(x);
}

void m158_void_statement_expression(void) {
    (void)({ m158_sink(); });
}

int m158_computed_goto(int which) {
    void *target = which ? &&one : &&two;
    goto *target;
one:
    return 1;
two:
    return 2;
}
