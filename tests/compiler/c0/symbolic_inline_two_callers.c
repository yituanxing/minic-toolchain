/* Two callers must not share caller-specific symbolic facts in a single AST. */
static int first_value = 11;
static int second_value = 29;

static inline int read_selected(int *value, int condition) {
    if (condition)
        return *value;
    return -*value;
}

static inline int forward_selected(int *value, int condition) {
    return read_selected(value, condition);
}

int first_route(void) {
    return forward_selected(&first_value, 1);
}

int second_route(void) {
    return forward_selected(&second_value, 0);
}

int dynamic_route(int *value, int condition) {
    return forward_selected(value, condition);
}

int main(void) {
    return first_route() == 11 && second_route() == -29 &&
                   dynamic_route(&first_value, 1) == 11 &&
                   dynamic_route(&second_value, 0) == -29
               ? 0 : 1;
}
