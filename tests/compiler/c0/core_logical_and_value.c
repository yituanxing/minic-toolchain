struct core_m19_node {
    struct core_m19_node *next;
    struct core_m19_node *prev;
};

int core_m19_rhs_calls;

int core_m19_rhs(int value) {
    core_m19_rhs_calls = core_m19_rhs_calls + 1;
    return value;
}

int core_m19_plain(int left, int right) {
    return left && right;
}

int core_m19_short_false(void) {
    core_m19_rhs_calls = 0;
    return 0 && core_m19_rhs(7);
}

int core_m19_short_true(void) {
    core_m19_rhs_calls = 0;
    return 2 && core_m19_rhs(7);
}

int core_m19_get_rhs_calls(void) {
    return core_m19_rhs_calls;
}

int core_m19_nested(int first, int second, int third) {
    return first && second && third;
}

int core_m19_cfg_statement_rhs(int left, int right) {
    return left && ({
               do {
                   if (right == 0)
                       right = 1;
               } while (0);
               right;
           });
}

int core_m19_cfg_initializer(int value) {
    int result = ({
        do {
            if (value == 0)
                value = 1;
        } while (0);
        value;
    });
    return result;
}

int core_m19_equality_cfg_rhs(const struct core_m19_node *left,
                              const struct core_m19_node *right,
                              int gate) {
    return left == ({
               do {
                   if (gate == 0)
                       gate = 1;
               } while (0);
               right;
           });
}

int core_m19_list_empty_careful_shape(const struct core_m19_node *head) {
    struct core_m19_node *next = ({
        struct core_m19_node *value =
            ({ (*(struct core_m19_node *const volatile *)&(head->next)); });
        __asm__ __volatile__("fence r,rw" : : : "memory");
        value;
    });
    return (next == head) &&
           (next == ({ (*(struct core_m19_node *const volatile *)&(head->prev)); }));
}
