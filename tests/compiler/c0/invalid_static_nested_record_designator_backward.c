struct Pair {
    int a;
    int b;
};

static struct Pair pair = {.b = 1, .a = 2};

int main(void) {
    return pair.a + pair.b;
}
