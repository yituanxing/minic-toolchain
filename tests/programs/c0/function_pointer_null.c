struct Hooks {
    int (*apply)(int);
};

static int increment(int value) {
    return value + 1;
}

static struct Hooks hooks = {increment};

int main(void) {
    if (hooks.apply == (void *)0) {
        return 1;
    }
    hooks.apply = (void *)0;
    if (hooks.apply != (void *)0) {
        return 2;
    }
    if ((void *)0 != hooks.apply) {
        return 3;
    }
    hooks.apply = 0;
    if (hooks.apply != 0) {
        return 4;
    }
    hooks.apply = increment;
    if (hooks.apply(41) != 42) {
        return 5;
    }
    return 0;
}
