/* Five-ISA CFG + GNU/native x86 differential without indirect branches.
 * Separate LP64 fixture covers computed-goto address relocations. */
static int enter_case(int take) {
    int value = 0;
    if (take) goto FORWARD;
    switch (sizeof(void *)) {
    case 1: FORWARD: value = 43; break;
    case 8: value = 42; break;
    default: value = 99; break;
    }
    return value;
}

int main(void) {
    return enter_case(0) == 42 && enter_case(1) == 43 ? 42 : 1;
}
