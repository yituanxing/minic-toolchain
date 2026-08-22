#include <stdio.h>

int core_m5b_global = 13;

int core_m5b_equal(int expected);
void core_m5b_set_if_equal(int value, int expected);

struct core_m11_node {
    struct core_m11_node *next;
    struct core_m11_node *prev;
};

int core_m11_pointer_equal(int *left, int *right);
int core_m11_member_pointer_equal(struct core_m11_node *node, struct core_m11_node *expected);

int main(void) {
    int before_equal;
    int before_unequal;
    int left_value;
    int right_value;
    struct core_m11_node node;
    struct core_m11_node other;

    before_equal = core_m5b_equal(13);
    before_unequal = core_m5b_equal(12);
    core_m5b_set_if_equal(21, 12);
    core_m5b_set_if_equal(29, 13);
    left_value = 1;
    right_value = 2;
    node.next = &other;
    node.prev = &node;
    other.next = &node;
    other.prev = &other;
    (void)printf("%d %d %d %d %d %d\n",
                 before_equal,
                 before_unequal,
                 core_m5b_global,
                 core_m11_pointer_equal(&left_value, &left_value),
                 core_m11_pointer_equal(&left_value, &right_value),
                 core_m11_member_pointer_equal(&node, &other));
    return 0;
}
