/* CFG pruning must distinguish an external source edge from an internal edge
 * that disappears with its discarded block. Case labels are dispatch entries
 * even inside a lexical if(0), independently of an ordinary goto. */
static int external_jump(void) {
    goto inside;
    return 1;
inside:
    return 42;
}

static int internal_jump(void) {
    if (0) {
        goto internal;
internal:
        return 99;
    }
    return 7;
}

static int dispatched_case(int value) {
    switch (value) {
        if (0) {
        case 3:
            return 31;
        }
        default:
            return 32;
    }
}

int main(void) {
    if (external_jump() != 42) return 1;
    if (internal_jump() != 7) return 2;
    if (dispatched_case(3) != 31) return 3;
    if (dispatched_case(2) != 32) return 4;
    return 0;
}
