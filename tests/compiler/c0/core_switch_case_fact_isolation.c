/* Every case is a separate possible switch entry; facts from a case before
 * it must not be reused when compiling a later independent case entry.
 * The fallthrough case ensures that an actual predecessor remains valid. */
static int independent_case(int selector, int initial) {
    int value = initial;
    switch (selector) {
    case 0:
        value = 1;
        break;
    case 1:
        if (value == 1)
            return 17;
        return 23;
    default:
        break;
    }
    return value;
}

static int fallthrough_case(int selector, int initial) {
    int value = initial;
    switch (selector) {
    case 0:
        value = 1;
        /* fall through */
    case 1:
        if (value == 1)
            return 17;
        return 23;
    default:
        break;
    }
    return value;
}

static int nested_case(int selector, int initial) {
    int value = initial;
    switch (selector) {
        if (selector >= 0) {
        case 0:
            value = 1;
            break;
        case 1:
            if (value == 1)
                return 17;
            return 23;
        }
    }
    return value;
}

int main(void) {
    if (independent_case(0, 0) != 1) return 1;
    if (independent_case(1, 0) != 23) return 2;
    if (independent_case(1, 1) != 17) return 3;
    if (fallthrough_case(0, 0) != 17) return 4;
    if (fallthrough_case(1, 0) != 23) return 5;
    if (fallthrough_case(1, 1) != 17) return 6;
    if (nested_case(0, 0) != 1) return 7;
    if (nested_case(1, 0) != 23) return 8;
    if (nested_case(1, 1) != 17) return 9;
    return 0;
}
