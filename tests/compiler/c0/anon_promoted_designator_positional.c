struct trace_event_field {
    const char *type;
    union {
        struct {
            const char *name;
            int size;
            int align;
            unsigned int is_signed:1;
            int filter_type;
        };
        int (*define_fields)(void);
    };
};

static struct trace_event_field fields[] = {
    { .type = "a", .name = "b", .size = 4, .align = 4,
      0, .filter_type = 9 },
    {},
};

struct outer {
    struct {
        int first;
        int second;
        int third;
    } inner;
    int tail;
};

static struct outer positional = {
    .inner.first = 1,
    2,
    .inner.third = 3,
    .tail = 4,
};

int main(void) {
    return !(fields[0].is_signed == 0 && fields[0].filter_type == 9 &&
             positional.inner.first == 1 && positional.inner.second == 2 &&
             positional.inner.third == 3 && positional.tail == 4);
}
