typedef int (UnaryFunction)(int);

struct ctl_table;
typedef unsigned long size_t;
typedef long loff_t;
/* Linux sysctl.h shape: a typedef names the function type itself. */
typedef int proc_handler(struct ctl_table *ctl, int write, void *buffer,
                         size_t *lenp, loff_t *ppos);

struct ProcOps {
    proc_handler *handler;
};

struct Ops {
    int (*run)(int);
};

extern int (*external_hook)(int);

static int add_one(int value)
{
    return value + 1;
}

static int apply(int (*function)(int), int value)
{
    return function(value);
}

static int proc_impl(struct ctl_table *ctl, int write, void *buffer,
                     size_t *lenp, loff_t *ppos)
{
    (void)ctl;
    (void)buffer;
    (void)lenp;
    (void)ppos;
    return write;
}

static int apply_proc(proc_handler *handler)
{
    return handler((struct ctl_table *)0, 42, (void *)0, (size_t *)0, (loff_t *)0);
}

int main(void)
{
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42);
}
