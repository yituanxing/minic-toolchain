struct Hooks {
    int (*binary)(int, int);
    int (*unary)(int);
};

static int add(int left, int right)
{
    return left + right;
}

static int increment(int value)
{
    return value + 1;
}

static struct Hooks hooks = { add, increment };

int main(void)
{
    if (hooks.binary(20, 22) != 42) {
        return 1;
    }
    if (hooks.unary(hooks.binary(3, 4)) != 8) {
        return 2;
    }
    return 0;
}
