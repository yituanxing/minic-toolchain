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

static int left_effect(void) {
    return 11;
}

static int init_effect(void) {
    return 22;
}

/* Linux iov_iter_ubuf shape: assign a designated record compound literal. */
void assign_holder(struct Holder *out, void *ptr, size_t count) {
    *out = (struct Holder){
        .tag = 1,
        .ptr = ptr,
        .count = count,
    };
}

int compound_member(void) {
    return ((struct Holder){.tag = 7}).tag;
}

/* Linux path_put_init shape: an empty compound literal means all-zero. */
void clear_holder(struct Holder *out) {
    *out = (struct Holder){};
}

int local_empty_initializer(void) {
    struct Holder holder = {};
    return holder.tag + (int)holder.count;
}

int compound_address_and_order(void) {
    int left = left_effect();
    struct Holder *holder = &((struct Holder){
        .tag = init_effect(),
        .ptr = (void *)0,
        .count = 3,
    });
    return left + holder->tag + (int)holder->count;
}

typedef struct {
    unsigned long val;
} kernel_cap_t;

/* Linux capability.h shape: positional compound literal from runtime expressions. */
kernel_cap_t cap_combine_shape(const kernel_cap_t a, const kernel_cap_t b) {
    return (kernel_cap_t){a.val | b.val};
}

int positional_member(void) {
    kernel_cap_t a = {.val = 5};
    kernel_cap_t b = {.val = 10};
    kernel_cap_t result = cap_combine_shape(a, b);
    return (int)result.val;
}

struct NestedInner {
    unsigned long first;
    unsigned long second;
};

struct NestedOuter {
    struct NestedInner inner;
    int count;
};

int nested_designated_braces(int value) {
    struct NestedOuter item = {
        .inner = {3, 4},
        .count = value,
    };
    return (int)(item.inner.first + item.inner.second) + item.count;
}

struct FlexibleRuntime {
    int tag;
    unsigned long count;
    unsigned char payload[];
};

void assign_flexible_prefix(struct FlexibleRuntime *out, int tag, unsigned long count) {
    *out = (struct FlexibleRuntime){
        .tag = tag,
        .count = count,
    };
}

void assign_flexible_prefix_positional(struct FlexibleRuntime *out, int tag, unsigned long count) {
    *out = (struct FlexibleRuntime){tag, count};
}

void clear_flexible_prefix(struct FlexibleRuntime *out) {
    *out = (struct FlexibleRuntime){};
}
