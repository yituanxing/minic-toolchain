struct Pair {
    int left;
    int right;
};

static const char *panic_later, *panic_param;
static int **deep, *shallow;
static struct Pair first_pair, second_pair;

int main(void) {
    if (panic_later != 0 || panic_param != 0 || deep != 0 || shallow != 0) {
        return 1;
    }
    return first_pair.left + first_pair.right + second_pair.left + second_pair.right;
}
