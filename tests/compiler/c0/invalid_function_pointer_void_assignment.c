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
    hooks.apply = object_pointer;
    return 0;
}
