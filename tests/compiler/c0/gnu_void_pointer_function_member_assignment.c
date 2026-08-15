struct Hooks {
    int (*apply)(int);
};

static int increment(int value) {
    return value + 1;
}

static struct Hooks hooks = {increment};

static int assign_object_pointer(struct Hooks *target, void *object_pointer) {
    target->apply = object_pointer;
    return 0;
}

int main(void) {
    int value;
    return assign_object_pointer(&hooks, &value);
}
