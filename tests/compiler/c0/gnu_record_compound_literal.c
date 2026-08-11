typedef unsigned long size_t;

struct Holder {
    int tag;
    union {
        struct {
            void *ptr;
            size_t count;
        };
        size_t raw;
    };
};

static int left_effect(void)
{
    return 11;
}

static int init_effect(void)
{
    return 22;
}

/* Linux iov_iter_ubuf shape: assign a designated record compound literal. */
void assign_holder(struct Holder *out, void *ptr, size_t count)
{
    *out = (struct Holder) {
        .tag = 1,
        .ptr = ptr,
        .count = count,
    };
}

int compound_member(void)
{
    return ((struct Holder) { .tag = 7 }).tag;
}

int compound_address_and_order(void)
{
    int left = left_effect();
    struct Holder *holder = &((struct Holder) {
        .tag = init_effect(),
        .ptr = (void *)0,
        .count = 3,
    });
    return left + holder->tag + (int)holder->count;
}
