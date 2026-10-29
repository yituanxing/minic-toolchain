struct triple { int a, b, c; };

int designated_then_positional(int seed) {
    struct triple v = {
        .a = (seed + 1) * 1,
        (seed + 2) * 1,
        (seed + 3) * 1
    };
    return v.a * 100 + v.b * 10 + v.c;
}

int main(void) { return designated_then_positional(4) == 567 ? 0 : 1; }
