struct Hooks {
    int (*apply)(int);
};

static int increment(int value) {
    return value + 1;
}

static struct Hooks hooks = {increment};

static int compare_object_pointer(struct Hooks *target, void *object_pointer) {
    if (target->apply == object_pointer) {
        return 1;
    }
    return 0;
}

int main(void) {
    int value;
    return compare_object_pointer(&hooks, &value);
}
