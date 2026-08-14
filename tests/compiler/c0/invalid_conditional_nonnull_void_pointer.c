struct node {
    int value;
};
int bad(int condition, struct node *node) {
    return (condition ? node : (void *)1)->value;
}
