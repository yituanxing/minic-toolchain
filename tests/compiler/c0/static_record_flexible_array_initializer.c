struct namespace_stub {
    int marker;
};

struct id_slot {
    int nr;
    struct namespace_stub *ns;
};

struct id_table {
    int level;
    struct id_slot numbers[];
};

struct namespace_stub init_namespace = { .marker = 7 };

struct id_table init_table = {
    .level = 0,
    .numbers = { {
        .nr = 0,
        .ns = &init_namespace,
    }, },
};

int static_record_fam_probe(void) {
    return init_table.numbers[0].ns == &init_namespace;
}
