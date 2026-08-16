int core_diff_math(int x, int y) {
    return -(x + y);
}

int core_diff_branch(int x) {
    if (x) {
        return 7;
    }
    return -3;
}

int core_diff_zero(int x) {
    return !x;
}

int core_diff_ninth(int a0,
                    int a1,
                    int a2,
                    int a3,
                    int a4,
                    int a5,
                    int a6,
                    int a7,
                    int a8) {
    return a8;
}

int core_diff_pointer_zero(int *value) {
    return !value;
}
