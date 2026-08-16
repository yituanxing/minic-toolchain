struct FixedRecord {
    unsigned long left;
    unsigned long right;
};

static struct FixedRecord zero_records[3];
static struct FixedRecord page_records[2]
    __attribute__((section(".bss..page_aligned")))
    __attribute__((aligned(4096)));
static const struct FixedRecord initialized_records[2] = {
    {1, 2},
    {3, 4},
};

unsigned long read_fixed_record_arrays(void) {
    return zero_records[2].right + page_records[1].left +
           initialized_records[0].left + initialized_records[1].right;
}
