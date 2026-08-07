struct Pair {
    int left;
    int right;
};

static struct Pair global_pair = {0, 0};

int main(void)
{
    struct Pair local_pair;
    struct Pair *pointer;

    pointer = &local_pair;
    local_pair.left = 14;
    pointer->right = 23;

    global_pair.left = local_pair.left;
    global_pair.right = pointer->right;
    return global_pair.left + global_pair.right;
}
