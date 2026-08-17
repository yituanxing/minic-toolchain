struct riscv_isa_ext_data {
    const char *name;
    const char *property;
    unsigned int id;
};

extern const struct riscv_isa_ext_data riscv_isa_ext[];

const struct riscv_isa_ext_data riscv_isa_ext[] = {
    {
        .name = "zicntr",
        .property = "riscv,isa-base",
        .id = 1,
    },
    {
        .name = "zifencei",
        .property = 0,
        .id = 2,
    },
};

struct stats_desc {
    unsigned int flags;
    int exponent;
    unsigned int size;
    unsigned int bucket_size;
    unsigned int offset;
};

struct kvm_stats_desc {
    struct stats_desc desc;
    char name[16];
};

const struct kvm_stats_desc kvm_stats[] = {
    {
        {
            .flags = 1,
            .exponent = -9,
            .size = 1,
            .bucket_size = 0,
            .offset = 4,
        },
        .name = "halt_wait",
    },
    {
        {
            .flags = 2,
            .exponent = 0,
            .size = 8,
            .bucket_size = 1,
            .offset = 12,
        },
        .name = "signal",
    },
};

const struct riscv_isa_ext_data fixed_record_array[1] = {
    {
        .name = "fixed",
        .property = 0,
        .id = 3,
    },
};

enum designated_ext_slot {
    DESIGNATED_EXT_SLOT = 3,
};

const struct riscv_isa_ext_data designated_ext[] = {
    [DESIGNATED_EXT_SLOT] = {
        .name = "designated",
        .property = 0,
        .id = 9,
    },
};
