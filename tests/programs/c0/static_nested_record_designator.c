struct Inner {
    int a;
    int b;
};

struct Outer {
    int prefix;
    struct Inner inner;
    int suffix;
};

static struct Outer state = {3, {.b = 7}, 9};

int main(void) {
    return state.prefix == 3 && state.inner.a == 0 && state.inner.b == 7 && state.suffix == 9 ? 0
                                                                                              : 1;
}
