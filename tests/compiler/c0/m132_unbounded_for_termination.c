static int terminal_loop(int value) {
    for (;;) {
        if (value)
            return value;
    }
}

static int breakable_loop(int value) {
    for (;;) {
        if (value)
            break;
        value = 1;
    }
    return value;
}

int main(void) {
    return terminal_loop(1) == 1 && breakable_loop(0) == 1 ? 0 : 1;
}
