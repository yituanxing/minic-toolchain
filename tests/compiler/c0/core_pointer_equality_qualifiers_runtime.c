#include <stdio.h>

struct core_m16_node {
    struct core_m16_node *next;
    struct core_m16_node *prev;
};

int core_m16_list_is_first(const struct core_m16_node *list, const struct core_m16_node *head);
int core_m16_list_is_last(const struct core_m16_node *list, const struct core_m16_node *head);
int core_m16_qualified_equal(struct core_m16_node *left, const struct core_m16_node *right);
int core_m16_qualified_not_equal(struct core_m16_node *left, const struct core_m16_node *right);
int core_m16_explicit_qualified_member_cast(const struct core_m16_node *list,
                                            const struct core_m16_node *head);
int core_m16_null_equal(const struct core_m16_node *node);
int core_m16_void_equal(struct core_m16_node *left, const void *right);

int main(void) {
    struct core_m16_node first;
    struct core_m16_node head;

    first.next = &head;
    first.prev = &head;
    head.next = &first;
    head.prev = &first;

    printf("%d %d %d %d %d %d %d %d %d\n",
           core_m16_list_is_first(&first, &head),
           core_m16_list_is_first(&first, &first),
           core_m16_list_is_last(&first, &head),
           core_m16_qualified_equal(&first, &first),
           core_m16_qualified_not_equal(&first, &head),
           core_m16_explicit_qualified_member_cast(&first, &head),
           core_m16_null_equal(&first),
           core_m16_null_equal(0),
           core_m16_void_equal(&first, &first));
    return 0;
}
