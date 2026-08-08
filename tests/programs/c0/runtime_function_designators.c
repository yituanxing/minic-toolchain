struct Hooks {
    int (*apply)(int);
};

static int increment(int value) {
    return value + 1;
}

static int decrement(int value) {
    return value - 1;
}

static struct Hooks hooks = {decrement};

static int shadow(void) {
    int increment;
    increment = 7;
    return increment;
}

int main(void) {
    if (hooks.apply(10) != 9) {
        return 1;
    }
    hooks.apply = increment;
    if (hooks.apply != increment) {
        return 2;
    }
    if (increment != hooks.apply) {
        return 3;
    }
    if (hooks.apply(41) != 42) {
        return 4;
    }
    hooks.apply = decrement;
    if (hooks.apply(10) != 9) {
        return 5;
    }
    if (shadow() != 7) {
        return 6;
    }
    return 0;
}
