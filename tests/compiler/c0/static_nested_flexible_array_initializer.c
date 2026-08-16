struct stats_desc {
    unsigned int flags;
    short exponent;
    unsigned short size;
    unsigned int offset;
    unsigned int bucket_size;
    char name[];
};

struct wrapped_stats_desc {
    struct stats_desc desc;
    const char *tag;
    char name[8];
};

const struct wrapped_stats_desc nested_fam_rows[] = {
    {
        {
            .flags = 1,
            .exponent = -1,
            .size = 2,
            .offset = 4,
            .bucket_size = 0,
        },
        .tag = "vm",
        .name = "alpha",
    },
    {
        {
            .flags = 3,
            .exponent = 0,
            .size = 1,
            .offset = 8,
            .bucket_size = 16,
        },
        .tag = "vcpu",
        .name = "beta",
    },
};
