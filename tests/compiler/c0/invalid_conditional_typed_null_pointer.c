struct node {
    int value;
};
struct other_node {
    int value;
};
int bad(int condition, struct node *node) {
    return (condition ? node : (struct other_node *)0)->value;
}
