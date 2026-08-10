__attribute__((__format__(printf, 1, 2)))
void panic_like(const char *fmt, ...) __attribute__((__noreturn__)) __attribute__((__cold__));

static int probe(void) {
    return 0;
}

int main(void) {
    return probe();
}
