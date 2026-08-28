typedef struct Pair {
    unsigned long pointer_bits;
    unsigned long length;
} Pair;

typedef int (*record_fn)(int, Pair);
typedef int (*variadic_fn)(const char *, ...);

static int consume_record(int bias, Pair pair) {
    return bias + (int)pair.length;
}

static int invoke_record(record_fn fn, Pair pair) {
    return fn(3, pair);
}

static int consume_variadic(const char *tag, ...) {
    return tag[0];
}

static int invoke_variadic(variadic_fn fn, int value, void *pointer) {
    return fn("v", value, pointer);
}

int main(void) {
    Pair pair = {0, 4};
    int a = invoke_record(consume_record, pair);
    int b = invoke_variadic(consume_variadic, 7, (void *)0);
    return a == 7 && b == 'v' ? 0 : 1;
}
