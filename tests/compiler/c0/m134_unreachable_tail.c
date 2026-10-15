int unreachable_expression(int x) {
    return x + 1;
    x = x + 2;
}

int unreachable_if(int x) {
    return x;
    if (x) {
        return 9;
    }
}

int unreachable_while(int x) {
    return x;
    while (x) {
        x = x - 1;
    }
}

int top_level_label_reentry(int x) {
    if (x) {
        goto live;
    }
    return 3;
live:
    return 4;
}
