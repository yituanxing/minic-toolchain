static int target(void) {
    return 7;
}

int main(void) {
    int (*function_pointer)(void) = target;
    void *opaque = function_pointer;
    if (opaque != function_pointer) {
        return 1;
    }
    if (function_pointer != opaque) {
        return 2;
    }
    return opaque == target && target == opaque ? 0 : 3;
}
