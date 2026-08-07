struct Pair {
    int left;
    int right;
};

int main(void)
{
    struct Pair pair;
    struct Pair *pointer;

    pointer = &pair;
    pair.left = 14;
    pointer->right = 23;
    return pair.left + pointer->right;
}
