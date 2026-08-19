struct child_value {
    int value;
};

struct static_designator_table {
    int val[2];
    struct child_value base[2];
    int *ptr[2];
};

static int anchor;
static struct static_designator_table table = {
    .val[1] = 9,
    .val[0] = 7,
    .base[1] = { .value = 13 },
    .base[0] = { .value = 11 },
    .ptr[1] = &anchor,
    .ptr[0] = 0,
};

int static_record_array_member_designator_probe(void) {
    return 0;
}
