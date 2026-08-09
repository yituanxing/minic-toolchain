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
