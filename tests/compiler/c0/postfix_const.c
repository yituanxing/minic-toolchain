static int same_first(char const *left, const char *right) {
    return left[0] == right[0];
}

typedef char *text;

static int use_typedef_const(text const value) {
    return value[0];
}

int main(void) {
    char value[] = "x";
    return same_first(value, value) && use_typedef_const(value) == 'x' ? 0 : 1;
}
