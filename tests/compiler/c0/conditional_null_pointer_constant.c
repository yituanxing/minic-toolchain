struct node {
    const char *name;
    int value;
};

struct other_node {
    int value;
};

struct node *choose_integer_zero(int condition, struct node *node) {
    return condition ? node : 0;
}

struct node *choose_void_zero_right(int condition, struct node *node) {
    return condition ? node : (void *)0;
}

struct node *choose_void_zero_left(int condition, struct node *node) {
    return condition ? (void *)0 : node;
}

const struct node *choose_const_preserved(int condition, const struct node *node) {
    return condition ? node : (void *)0;
}

int member_after_conditional(int condition, struct node *node) {
    return (condition ? node : (void *)0)->value;
}

int linux_statement_expression_shape(int condition, struct node *node) {
    return ({
               struct node *saved = node;
               condition ? ({
                   void *raw = (void *)saved;
                   (struct node *)raw;
               })
                         : ((void *)0);
           })
        ->value;
}
