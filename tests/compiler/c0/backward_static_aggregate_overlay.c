struct node {
    long value;
};
struct pair {
    long left;
    long right;
};
struct holder {
    struct node *slots[2];
    struct pair pair;
    long tail;
};
static struct node node0 = {7};
static struct holder object = {
    .tail = 9,
    .slots = {&node0, &node0},
    .pair = (struct pair){3, 5},
};
int main(void) {
    return (int)(object.slots[0]->value + object.slots[1]->value + object.pair.left +
                 object.pair.right + object.tail - 31);
}
