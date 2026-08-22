struct core_m20_node {
    struct core_m20_node *next;
};

unsigned long core_m20_for_assign(struct core_m20_node *head) {
    struct core_m20_node *pos;
    unsigned long count = 0;
    for (pos = head->next; pos != head; pos = pos->next)
        count = count + 1;
    return count;
}

unsigned long core_m20_postincrement(unsigned long value) {
    value++;
    return value;
}

unsigned long core_m20_list_count_nodes(struct core_m20_node *head) {
    struct core_m20_node *pos;
    unsigned long count = 0;
    for (pos = head->next; pos != head; pos = pos->next)
        count++;
    return count;
}
