int core_m27_switch_simple(int value) {
    switch (value) {
    case 1:
        return 10;
    case 2:
    case 3:
        return 20;
    default:
        return 30;
    }
}

int core_m27_switch_range(int value) {
    switch (value) {
    case 4 ... 7:
        return 1;
    default:
        return 0;
    }
}

int core_m27_switch_fallthrough(int value) {
    int result;

    result = 0;
    switch (value) {
    case 1:
        result = result + 1;
    case 2:
        result = result + 2;
        break;
    default:
        result = 9;
    }
    return result;
}

int core_m27_printk_get_level(const char *buffer) {
    if (buffer[0] == '\001' && buffer[1]) {
        switch (buffer[1]) {
        case '0' ... '7':
        case 'c':
            return buffer[1];
        }
    }
    return 0;
}


int core_m27_switch_nested_case(int selector, int cond) {
    int result;

    result = 0;
    switch (selector) {
    case 3:
    case 4:
    case 5:
        result = 3;
        if (cond) {
    case 0:
    case 1:
    case 2:
            result = result + 10;
        }
        break;
    default:
        result = 99;
    }
    return result;
}
