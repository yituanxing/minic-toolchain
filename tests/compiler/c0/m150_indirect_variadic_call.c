typedef int (*sink_fn)(const char *, ...);

static int sink(const char *tag, ...) {
    return tag[0];
}

static int invoke(sink_fn fn, int value, void *pointer) {
    return fn("v", value, pointer);
}

int main(void) {
    return invoke(sink, 7, (void *)0) == 'v' ? 0 : 1;
}
