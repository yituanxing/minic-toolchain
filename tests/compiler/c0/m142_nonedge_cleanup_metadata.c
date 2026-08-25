static void cleanup_int(int *value) {
    *value += 1;
}

static int cleanup_assignment(int seed) {
    int guard __attribute__((cleanup(cleanup_int))) = seed;
    int result = 0;
    result = guard + 7;
    return result;
}

static int cleanup_internal_loop_label(int seed) {
    int total = 0;
    int i;
    for (i = 0; i < 3; i += 1) {
        int guard __attribute__((cleanup(cleanup_int))) = i;
        while (guard < 0) {
            guard += 1;
        }
        total += guard;
    }
    return total + seed;
}

int main(void) {
    return cleanup_assignment(2) == 9 &&
           cleanup_internal_loop_label(4) == 7 ? 0 : 1;
}
