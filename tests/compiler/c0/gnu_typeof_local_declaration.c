struct link {
    struct link *next;
};

static struct link *copy_next(const struct link *head) {
    return ({
        typeof(*&head->next) saved = head->next;
        saved;
    });
}

int main(void) {
    struct link node;

    node.next = &node;
    return copy_next(&node) == &node ? 0 : 1;
}
