struct ClockInfo {
    const char *name;
    unsigned int id;
    const unsigned int *table;
};
static const unsigned int table[] = { 3U };
static const struct {
    struct ClockInfo common[1];
    struct ClockInfo drp[0];
} invalid_clks = {
    .common = { { .name = "valid", .id = 1, .table = table } },
    /* This specifies an element that cannot fit into a zero-length array.
     * GCC diagnoses excess elements; MiniC must not silently emit a ghost. */
    .drp = { { .name = "unexpected", .id = 2, .table = table } },
};
int main(void) { return invalid_clks.common[0].id; }
