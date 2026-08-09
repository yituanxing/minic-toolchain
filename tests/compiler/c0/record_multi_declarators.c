struct Link {
    int value, flags;
    struct Link *previous, *next;
};

int read_link(struct Link *link) {
    return link->next->value + link->flags;
}
