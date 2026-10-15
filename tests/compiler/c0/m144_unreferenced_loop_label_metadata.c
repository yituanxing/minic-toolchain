static void cleanup_int(int *value) {
    *value += 1;
}

static int cleanup_loop_tail(int seed) {
    int total = seed;
    int i;
    for (i = 0; i < 3; i += 1) {
        int guard __attribute__((cleanup(cleanup_int))) = i;
        while (guard < 1)
            guard += 1;
        total += guard;
    }
    return total;
}

static int ordinary_source_label(int x) {
    if (x > 0)
        goto done;
    x = 7;
done:
    return x;
}

int main(void) {
    return cleanup_loop_tail(2) == 6 &&
           ordinary_source_label(3) == 3 &&
           ordinary_source_label(0) == 7 ? 0 : 1;
}
