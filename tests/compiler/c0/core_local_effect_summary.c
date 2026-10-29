/* Loop-entry facts must be killed by side effects in an lvalue address and
 * by the extracted tail of a GNU statement expression. */
static int gnu_tail_update(void) {
    int value = 0;
    while (({ value++; })) {
        return 91;
    }
    if (value != 1)
        return 11;
    return 0;
}

static int indexed_lvalue_update(int iterations) {
    int data[3];
    int index = 0;
    data[0] = 0;
    while (iterations > 0) {
        data[index++] = 7;
        break;
    }
    if (index != 1)
        return 12;
    if (data[0] != 7)
        return 13;
    return 0;
}

int main(void) {
    if (gnu_tail_update() != 0) return 1;
    if (indexed_lvalue_update(1) != 0) return 2;
    return 0;
}
