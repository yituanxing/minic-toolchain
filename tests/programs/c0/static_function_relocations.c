typedef struct Hooks {
    int (*first)(int value);
    int (*second)(int value);
} Hooks;

static int add_one(int value)
{
    return value + 1;
}

static int add_two(int value)
{
    return value + 2;
}

static Hooks hooks = { add_one, add_two };

int main(void)
{
    if (hooks.first == 0) {
        return 1;
    }
    if (hooks.second == 0) {
        return 2;
    }
    return 0;
}
