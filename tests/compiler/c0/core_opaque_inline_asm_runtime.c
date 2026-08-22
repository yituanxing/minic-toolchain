#include <stdio.h>

struct core_m18_node {
    struct core_m18_node *next;
    struct core_m18_node *prev;
};

void core_m18_fence_only(void);
void core_m18_release_shape(struct core_m18_node *entry);
void core_m18_two_fences(void);

int main(void) {
    struct core_m18_node node;

    node.next = 0;
    node.prev = 0;
    core_m18_fence_only();
    core_m18_release_shape(&node);
    core_m18_two_fences();
    printf("%d %d\n", node.next == &node, node.prev == &node);
    return 0;
}
