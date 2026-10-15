struct pair { int left; int right; };

int main(void) {
    struct pair seed = { 3, 4 };
    struct pair *ptr = &seed;
    struct pair values[] = { *ptr, { 5, 6 } };
    struct pair fixed[2] = { *ptr, { 7, 8 } };
    return values[0].left == 3 && values[0].right == 4 &&
                   values[1].left == 5 && values[1].right == 6 &&
                   fixed[0].left == 3 && fixed[0].right == 4 &&
                   fixed[1].left == 7 && fixed[1].right == 8
               ? 0
               : 1;
}
