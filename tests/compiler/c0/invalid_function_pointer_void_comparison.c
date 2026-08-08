struct Hooks {
    int (*apply)(int);
};

static int increment(int value) {
    return value + 1;
}

static struct Hooks hooks = {increment};

int main(void) {
    int value;
    void *object_pointer;
    object_pointer = &value;
    if (hooks.apply == object_pointer) {
        return 1;
    }
    return 0;
}
