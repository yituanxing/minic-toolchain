static int while_terminal(int value) {
    while (1) {
        if (value)
            return value;
    }
}

static int do_while_terminal(int value) {
    do {
        if (value)
            return value;
    } while (1);
}

static int while_breakable(int value) {
    while (1) {
        if (value)
            break;
        value = 1;
    }
    return value;
}

static int while_false(int value) {
    while (0)
        value = 99;
    return value;
}

int main(void) {
    return while_terminal(1) == 1 &&
           do_while_terminal(1) == 1 &&
           while_breakable(0) == 1 &&
           while_false(7) == 7 ? 0 : 1;
}
