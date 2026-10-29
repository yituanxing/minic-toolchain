static int cleanup_trace;

static void cleanup_record(int *value) {
    cleanup_trace = cleanup_trace * 10 + *value;
}

static int return_order(void) {
    int value __attribute__((__cleanup__(cleanup_record))) = 7;

    cleanup_trace = 0;
    return cleanup_trace * 10 + value;
}

static int normal_exit_order(void) {
    cleanup_trace = 0;
    {
        int first __attribute__((cleanup(cleanup_record))) = 1;
        int second __attribute__((__cleanup__(cleanup_record))) = 2;
        (void)first;
        (void)second;
    }
    return cleanup_trace;
}

static int for_break_cleanup(void) {
    cleanup_trace = 0;
    for (int guard __attribute__((__cleanup__(cleanup_record))) = 4; 1; ) {
        break;
    }
    return cleanup_trace;
}

static int forward_goto_cleanup(void) {
    cleanup_trace = 0;
    {
        int guard __attribute__((cleanup(cleanup_record))) = 6;
        if (guard != 0) goto outside_guard;
    }
outside_guard:
    return cleanup_trace;
}

static int same_scope_goto_keeps_guard(void) {
    cleanup_trace = 0;
    {
        int guard __attribute__((cleanup(cleanup_record))) = 7;
        goto inside_guard;
inside_guard:
        if (cleanup_trace != 0 || guard != 7) return -1;
    }
    return cleanup_trace;
}

static int backward_goto_cleanup(void) {
    int done = 0;
    cleanup_trace = 0;
again:
    if (done) return cleanup_trace;
    {
        int guard __attribute__((cleanup(cleanup_record))) = 8;
        done = 1;
        goto again;
    }
}

static int linux_guard_shape(void) {
    cleanup_trace = 0;
    for (int guard __attribute__((__cleanup__(cleanup_record))) = 5;
         1;
         ({ goto cleanup_break; }))
        if (0) {
        cleanup_break:
            break;
        } else {
            (void)guard;
        }
    return cleanup_trace;
}

int main(void) {
    int returned;

    returned = return_order();
    if (returned != 7 || cleanup_trace != 7) {
        return 11;
    }
    if (normal_exit_order() != 21) {
        return 12;
    }
    if (for_break_cleanup() != 4) {
        return 13;
    }
    if (forward_goto_cleanup() != 6) {
        return 15;
    }
    if (same_scope_goto_keeps_guard() != 7) {
        return 16;
    }
    if (backward_goto_cleanup() != 8) {
        return 17;
    }
    if (linux_guard_shape() != 5) {
        return 14;
    }
    return 0;
}
