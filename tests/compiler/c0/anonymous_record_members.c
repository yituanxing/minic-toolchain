struct BranchData {
    const char *func;
    const char *file;
    unsigned line;
    union {
        struct {
            unsigned long correct;
            unsigned long incorrect;
        };
        struct {
            unsigned long miss;
            unsigned long hit;
        };
        unsigned long miss_hit[2];
    };
};

struct FunctionProto {
    unsigned ret_type;
    union {
        struct {
            unsigned arg1_type;
            unsigned arg2_type;
        };
        unsigned arg_type[2];
    };
};

static const struct FunctionProto promoted_static_proto = {
    .ret_type = 7,
    .arg1_type = 11,
    .arg2_type = 22,
};

unsigned long read_correct(struct BranchData *data) {
    return data->correct;
}

unsigned long read_hit(struct BranchData *data) {
    return data->hit;
}

unsigned long read_second_counter(struct BranchData *data) {
    return data->miss_hit[1];
}

unsigned long branch_data_size(void) {
    return sizeof(struct BranchData);
}

unsigned read_promoted_static_arg1(void) {
    return promoted_static_proto.arg1_type;
}

unsigned read_promoted_static_arg2(void) {
    return promoted_static_proto.arg2_type;
}
