int main(void) {
    int i = 0;
    int total = 0;

    for (i = 0; i < 6; i++) {
        if (i == 2) {
            continue;
        }
        switch (i) {
        case 4:
            continue;
        default:
            break;
        }
        total += i;
    }
    if (total != 9) {
        return 1;
    }

    i = 0;
    while (i < 5) {
        i = i + 1;
        if (i == 3) {
            continue;
        }
        total += 1;
    }
    if (total != 13) {
        return 2;
    }
    return 0;
}
