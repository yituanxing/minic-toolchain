static int classify(int value)
{
    int result = 0;

    switch (value) {
    case 1:
    case 2:
        result = 10;
        break;
    case 3:
        result = 20;
    case 4:
        result = result + 1;
        break;
    default:
        result = 99;
    }
    return result;
}

static int selector_once(int *count)
{
    *count = *count + 1;
    return 2;
}

static int nested_break_targets(int value)
{
    int result = 0;
    int running = 1;

    while (running) {
        switch (value) {
        case 7:
            result = 30;
            break;
        default:
            result = 40;
            break;
        }
        result = result + 1;
        break;
    }

    switch (value) {
    case 7:
        while (1) {
            result = result + 10;
            break;
        }
        result = result + 2;
        break;
    default:
        result = 0;
    }
    return result;
}

static int nested_switch(void)
{
    int result = 0;

    switch (8) {
    case 8:
        switch (9) {
        case 9:
            result = 5;
            break;
        default:
            result = 90;
        }
        result = result + 6;
        break;
    default:
        result = 99;
    }
    return result;
}

int main(void)
{
    int count = 0;
    int result = 0;

    if (classify(1) != 10) {
        return 1;
    }
    if (classify(2) != 10) {
        return 2;
    }
    if (classify(3) != 21) {
        return 3;
    }
    if (classify(4) != 1) {
        return 4;
    }
    if (classify(99) != 99) {
        return 5;
    }

    switch (selector_once(&count)) {
    case 2:
        result = 17;
        break;
    default:
        result = 99;
    }
    if (count != 1 || result != 17) {
        return 6;
    }
    if (nested_break_targets(7) != 43) {
        return 7;
    }
    if (nested_switch() != 11) {
        return 8;
    }

    return 0;
}
