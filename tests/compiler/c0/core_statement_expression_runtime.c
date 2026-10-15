#include <stdio.h>

struct core_m17_node {
    struct core_m17_node *next;
    struct core_m17_node *prev;
};

int core_m17_scalar_value(int value);
struct core_m17_node *core_m17_read_once_shape(const struct core_m17_node *head);
int core_m17_list_empty(const struct core_m17_node *head);
int core_m17_prefix_store(int *value);
int core_m17_prefix_call(int value);

int main(void) {
    struct core_m17_node head;
    struct core_m17_node other;
    int stored = 4;

    head.next = &head;
    head.prev = &head;
    other.next = &head;
    other.prev = &head;

    printf("%d %d %d %d %d %d\n",
           core_m17_scalar_value(9),
           core_m17_read_once_shape(&head) == &head,
           core_m17_list_empty(&head),
           core_m17_list_empty(&other),
           core_m17_prefix_store(&stored),
           core_m17_prefix_call(8));
    printf("stored=%d\n", stored);
    return 0;
}
