struct ClockInfo {
    const char *name;
    unsigned int id;
    const unsigned int *table;
};

static const unsigned int table_a[] = { 11U, 13U };
static const unsigned int table_b[] = { 17U, 19U };

/* GNU permits an explicitly empty trailing [0] member. It has zero storage
 * and zero scalar initializer slots, despite the nonempty record element. */
static const struct {
    struct ClockInfo common[2];
    struct ClockInfo drp[0];
} core_clks = {
    .common = {
        { .name = "a", .id = 1, .table = table_a },
        { .name = "b", .id = 2, .table = table_b },
    },
    .drp = { },
};

int main(void) {
    return core_clks.common[0].name[0] == 'a' &&
           core_clks.common[1].name[0] == 'b' &&
           core_clks.common[0].table[1] == 13U &&
           core_clks.common[1].table[1] == 19U &&
           core_clks.common[1].id == 2U &&
           sizeof(core_clks) == 2U * sizeof(struct ClockInfo) ? 0 : 1;
}
