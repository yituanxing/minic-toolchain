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


static int read_named(void) {
    return 17;
}

static int write_named(void) {
    return 23;
}

struct MiniNamedHook {
    char name[8];
    int (*read_u64)(void);
    int (*write_u64)(void);
};

static struct MiniNamedHook named_hooks[] = {
    {
        .name = "shares",
        .read_u64 = read_named,
        .write_u64 = write_named,
    },
};

struct MiniExactTag {
    char tag[3];
    int marker;
};

static struct MiniExactTag exact_tags[] = {
    {
        .tag = "abc",
        .marker = 7,
    },
};

int read_named_hook(void) {
    return named_hooks[0].name[0] + named_hooks[0].name[5] + named_hooks[0].read_u64() +
           named_hooks[0].write_u64() + exact_tags[0].tag[2] + exact_tags[0].marker;
}
