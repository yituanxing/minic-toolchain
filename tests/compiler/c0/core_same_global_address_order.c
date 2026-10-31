/* Linux 6.6.143 mm/ksm.c: STABLE_NODE_DUP_HEAD is the address of an
 * in-object member, not an unknown integer or an unrelated linker symbol. */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};
static struct list_head migrate_nodes;
static struct list_head unrelated_nodes;

extern void impossible_early_sentinel(void)
    __attribute__((error("member address must follow object start")));
extern void impossible_late_sentinel(void)
    __attribute__((error("member address must precede one-past object")));

int stable_node_guard(void) {
    if (!(!(((struct list_head *)&migrate_nodes.prev) <= &migrate_nodes)))
        impossible_early_sentinel();
    if (!(!(((struct list_head *)&migrate_nodes.prev) >= &migrate_nodes + 1)))
        impossible_late_sentinel();
    return 19;
}

/* Different objects have linker-owned relative addresses.  Never fold this
 * source-level comparison as if they were a single aggregate. */
int unrelated_global_order(void) {
    return &migrate_nodes < &unrelated_nodes;
}

int main(void) {
    return stable_node_guard() == 19 ? 0 : 1;
}
