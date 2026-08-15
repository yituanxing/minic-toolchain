typedef long (*syscall_t)(unsigned long,
                          unsigned long,
                          unsigned long,
                          unsigned long,
                          unsigned long,
                          unsigned long,
                          unsigned long);

extern void *const compat_sys_call_table[];

struct early_entry {
    const void *data;
};

typedef void (*early_fn_t)(const void *);

long linux_shaped_syscall_assignment(unsigned long syscall) {
    syscall_t fn;

    fn = compat_sys_call_table[syscall];
    return fn(0, 0, 0, 0, 0, 0, 0);
}

void linux_shaped_early_assignment(const struct early_entry *entry, const void *arg) {
    early_fn_t fn;

    fn = (void *)entry->data;
    fn(arg);
}

void *gnu_function_pointer_to_void(syscall_t fn) {
    void *value;

    value = fn;
    return value;
}
