/* C8: a return's full-expression parser owns '=' and nested assignment. */
#ifdef MINIC_INVALID_RETURN_ASSIGNMENT
int main(void) {
    const int unchanged = 0;
    return unchanged = 1;
}
#else
static int assign_result(int left, int right) {
    return left = right + (left - left);
}

static int chained_result(int a, int b, int c) {
    return a = b = c;
}

static int conditional_result(int value, int condition) {
    return value = condition ? 7 : 11;
}

int main(void) {
    if (assign_result(3, 4) != 4) return 1;
    if (chained_result(1, 2, 5) != 5) return 2;
    if (conditional_result(2, 0) != 11) return 3;
    if (conditional_result(2, 1) != 7) return 4;
    return 0;
}
#endif
