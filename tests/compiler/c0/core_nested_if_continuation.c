int core_nested_then(int outer, int inner) {
    int value = 9;
    if (outer) {
        if (inner)
            return 1;
        value = 2;
    }
    return value;
}

int core_nested_both(int outer, int inner) {
    int value = 9;
    if (outer) {
        if (inner)
            return 1;
        value = 2;
    } else {
        if (inner)
            return 3;
        value = 4;
    }
    return value;
}
