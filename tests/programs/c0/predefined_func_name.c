static int check_func(void) {
    if (sizeof(__func__) != 11U) {
        return 1;
    }
    if (__func__[0] != 'c' || __func__[9] != 'c') {
        return 2;
    }
    if (&__func__[0] != &__func__[0]) {
        return 3;
    }
    return 0;
}

int main(void) {
    return check_func();
}
