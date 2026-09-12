static int first(void) {
    enum {
        ARG_shared = 1,
        ARG_first = 3,
    };
    return ARG_shared + ARG_first;
}

static int second(void) {
    enum {
        ARG_shared = 2,
        ARG_second = 4,
    };
    {
        enum {
            ARG_shared = 8,
        };
        if (ARG_shared != 8)
            return 99;
    }
    return ARG_shared + ARG_second;
}

int main(void) {
    return first() == 4 && second() == 6 ? 0 : 1;
}
