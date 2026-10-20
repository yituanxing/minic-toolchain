int multiplied_size(void) {
    char multiplied[1024 * 1024];
    return (int)sizeof(multiplied);
}

int grouped_size(void) {
    unsigned char grouped[(2 + 3) * (8 - 1)];
    return (int)sizeof(grouped);
}

int divided_size(void) {
    int divided[(24 / 3) + (7 % 4)];
    return (int)sizeof(divided);
}

int linux_siginfo_bound(void) {
    char dummy[(__alignof__(void *) < sizeof(short) ? sizeof(short) : __alignof__(void *))];
    return (int)sizeof(dummy);
}

struct RecordBound {
    char dummy[(__alignof__(void *) < sizeof(short) ? sizeof(short) : __alignof__(void *))];
    int tail;
};

int linux_siginfo_record_bound(void) {
    return (int)sizeof(((struct RecordBound *)0)->dummy);
}


/* GNU C accepts a string literal element as a compile-time array bound.
 * BusyBox's original libbb/vfork_daemon_rexec.c has exactly this shape:
 * BUILD_BUG_ON(CONFIG_BUSYBOX_EXEC_PATH[0] != '/').
 * Reject ordinary const global and runtime array reads (below fixture).
 */
#define STRING_BOUND_CHECK(expr) ((void)sizeof(char[1 - 2 * !!(expr)]))
int busybox_exec_path_string_bound(void) {
    STRING_BOUND_CHECK("/proc/self/exe"[0] != '/');
    STRING_BOUND_CHECK("alpha"[5] != '\\0');
    STRING_BOUND_CHECK("\\x2fbin"[0] != '/');
    return 0;
}

int main(void) {
    return busybox_exec_path_string_bound() == 0 && multiplied_size() == 1048576 && grouped_size() == 35 && divided_size() == 44 &&
                   linux_siginfo_bound() == 8 && linux_siginfo_record_bound() == 8
               ? 0
               : 1;
}
