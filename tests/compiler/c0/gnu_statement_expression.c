static int statement_value(int input) {
    int value = input;

    return ({
        do {
            value += 2;
        } while (0);
        value;
    });
}

static int statement_void(int input) {
    int value = input;

    ({
        value += 3;
        __asm__ __volatile__("" : : : "memory");
    });
    return value;
}

int main(void) {
    return statement_value(5) == 7 && statement_void(4) == 7 ? 0 : 1;
}
