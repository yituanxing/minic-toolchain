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

static void discard(int value)
{
    (void)value;
}

static int apply_local(int value)
{
    void (*notify)(int);
    int (*transform)(int);
    int (ordinary) = value;

    notify = discard;
    transform = add_one;
    notify(ordinary);
    return transform(ordinary);
}

struct Rq {
    int value;
};

static void observe_rq(struct Rq *rq)
{
    (void)rq;
}

static void apply_casted_callback(void *raw, struct Rq *rq)
{
    void (*function)(struct Rq *);

    function = (void (*)(struct Rq *))raw;
    function(rq);
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
    struct Rq rq;

    rq.value = 7;
    apply_casted_callback((void *)observe_rq, &rq);
    return (apply(add_one, 41) - 42) + (apply_proc(proc_impl) - 42) +
           (apply_local(41) - 42);
}
