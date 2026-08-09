static int same_first(char const *left, const char *right) {
    return left[0] == right[0];
}

typedef char *text;

static int use_typedef_const(text const value) {
    return value[0];
}

int main(void) {
    return same_first("x", "x") && use_typedef_const("x") == 'x' ? 0 : 1;
}
