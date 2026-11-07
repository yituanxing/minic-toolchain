struct BranchData {
    const char *func;
    const char *file;
    unsigned line;
    union {
        struct { unsigned long correct, incorrect; };
        struct { unsigned long miss, hit; };
        unsigned long miss_hit[2];
    };
};

extern unsigned long read_correct(struct BranchData *data);
extern unsigned long read_hit(struct BranchData *data);
extern unsigned long read_second_counter(struct BranchData *data);
extern unsigned long branch_data_size(void);

int main(void) {
    struct BranchData data = {0};

    data.correct = 13UL;
    data.incorrect = 27UL;
    return !(branch_data_size() == sizeof(data) && sizeof(data) == 40U &&
             read_correct(&data) == 13UL && read_hit(&data) == 27UL &&
             read_second_counter(&data) == 27UL);
}
