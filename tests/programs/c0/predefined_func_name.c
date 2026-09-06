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
    if (sizeof(__FUNCTION__) != sizeof(__func__)) {
        return 4;
    }
    if (__FUNCTION__[0] != 'c' || __FUNCTION__[9] != 'c') {
        return 5;
    }
    if (&__FUNCTION__[0] != &__func__[0]) {
        return 6;
    }
    return 0;
}

int main(void) {
    return check_func();
}
