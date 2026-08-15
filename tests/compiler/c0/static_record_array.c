typedef unsigned char MiniByte;

static const struct {
    MiniByte left;
    MiniByte right;
} priority[] = {
    {10, 10},
    {6, 6},
    {4}
};

int read_priority(int index) {
    return (int)priority[index].left + (int)priority[index].right;
}

typedef unsigned short MiniMode;

struct MiniCtlTable {
    const char *procname;
    void *data;
    int maxlen;
    MiniMode mode;
    int (*proc_handler)(void);
};

static struct MiniCtlTable sched_core_sysctls_like[] = {
    {}
};

int read_sysctl_size(void) {
    return (int)sizeof(sched_core_sysctls_like);
}
