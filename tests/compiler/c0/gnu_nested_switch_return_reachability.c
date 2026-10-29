/* A nested switch can return along every arm despite a trailing, dead break. */
static int nested_mode(unsigned product, unsigned mode) {
    switch (product) {
    case 11:
        switch (mode) {
        case 1: return 111;
        case 2: return 112;
        default: return -11;
        }
        break;
    case 22:
        switch (mode) {
        case 3: return 223;
        default: return -22;
        }
        break;
    default:
        return -1;
    }
}

/* A genuinely reachable break must still escape the switch. */
static int live_break(unsigned product, unsigned mode) {
    switch (product) {
    case 11:
        switch (mode) {
        case 1: return 111;
        default: break;
        }
        break;
    case 22:
        return 222;
    default:
        return -1;
    }
    return -77;
}

int main(void) {
    if (nested_mode(11, 1) != 111 || nested_mode(11, 2) != 112 ||
        nested_mode(11, 99) != -11 || nested_mode(22, 3) != 223 ||
        nested_mode(22, 0) != -22 || nested_mode(99, 0) != -1)
        return 1;
    if (live_break(11, 1) != 111 || live_break(11, 0) != -77 ||
        live_break(22, 0) != 222 || live_break(99, 0) != -1)
        return 2;
    return 0;
}
