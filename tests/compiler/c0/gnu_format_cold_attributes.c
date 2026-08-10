__attribute__((__format__(printf, 1, 2)))
void panic_like(const char *fmt, ...) __attribute__((__noreturn__)) __attribute__((__cold__));

static __attribute__((__format__(printf, 1, 0))) inline __attribute__((__gnu_inline__))
    __attribute__((__unused__)) __attribute__((__no_instrument_function__)) int
ftrace_vprintk_like(const char *fmt, void *ap) {
    (void)fmt;
    (void)ap;
    return 0;
}

inline static __attribute__((__unused__)) int reordered_static_inline(void) {
    return 0;
}

static int probe(void) {
    return ftrace_vprintk_like("x", (void *)0) + reordered_static_inline();
}

int main(void) {
    return probe();
}
