int m159_infinite_return_loop(int x) {
    do {
        if (x)
            return 1;
        x += 1;
    } while (1);
}

int m159_false_tail_not_stripped(int x) {
    do {
        x += 1;
    } while (0);
    return x;
}
