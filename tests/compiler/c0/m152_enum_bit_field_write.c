enum iter_state {
    ITER_INVALID,
    ITER_ACTIVE,
    ITER_DRAINED,
};

struct outer_state {
    int prefix;
    struct {
        void *btf;
        unsigned int btf_id;
        enum iter_state state : 2;
        int depth : 30;
    } iter;
};

enum value_type {
    VALUE_UNDEFINED,
    VALUE_FLAG,
    VALUE_STRING,
};

struct parameter {
    const char *key;
    enum value_type type : 8;
    char *string;
};

void set_iter_state(struct outer_state *st) {
    st->iter.state = ITER_ACTIVE;
}

int set_parameter_type(void) {
    struct parameter param;
    param.type = VALUE_FLAG;
    return 0;
}
