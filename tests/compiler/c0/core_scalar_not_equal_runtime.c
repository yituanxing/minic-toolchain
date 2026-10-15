#include <stdio.h>

struct core_m13_node {
    struct core_m13_node *next;
    struct core_m13_node *prev;
};

int core_m13_integer_not_equal(int left, int right);
int core_m13_pointer_not_equal(int *left, int *right);
int core_m13_member_pointer_not_equal(struct core_m13_node *node, struct core_m13_node *other);
int core_m13_list_condition(struct core_m13_node *new_node,
                            struct core_m13_node *prev,
                            struct core_m13_node *next);

int main(void) {
    int left = 1;
    int right = 2;
    struct core_m13_node prev;
    struct core_m13_node next;
    struct core_m13_node new_node;

    prev.next = &next;
    prev.prev = &prev;
    next.next = &next;
    next.prev = &prev;
    new_node.next = &new_node;
    new_node.prev = &new_node;

    printf("%d %d %d %d %d %d %d\n",
           core_m13_integer_not_equal(3, 3),
           core_m13_integer_not_equal(3, 4),
           core_m13_pointer_not_equal(&left, &left),
           core_m13_pointer_not_equal(&left, &right),
           core_m13_member_pointer_not_equal(&prev, &next),
           core_m13_member_pointer_not_equal(&prev, &prev),
           core_m13_list_condition(&new_node, &prev, &next));
    return 0;
}
