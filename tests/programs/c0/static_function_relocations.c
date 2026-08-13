static const char hook_name[] = "hooks";

typedef struct Hooks {
    const char *name;
    int (*first)(int value);
    int (*second)(int value);
    int early;
} Hooks;

static int add_one(int value) {
    return value + 1;
}

static int add_two(int value) {
    return value + 2;
}

static Hooks hooks = {hook_name, add_one, add_two, 0};

int main(void) {
    if (hooks.name == 0 || hooks.name[0] != 'h') {
        return 1;
    }
    if (hooks.first == 0 || hooks.first(3) != 4) {
        return 2;
    }
    if (hooks.second == 0 || hooks.second(3) != 5) {
        return 3;
    }
    if (hooks.early != 0) {
        return 4;
    }
    return 0;
}
