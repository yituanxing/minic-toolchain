/* A GNU -O2 constant-read oracle for an immutable array of records.
 * The field's flattened initializer slot is not the same as its byte offset. */
struct Mode {
    unsigned char id;
    unsigned short clock;
    int vtotal;
    unsigned long long flags;
};

static const struct Mode modes[4] = {
    { 8, 600, 262, 0x123456789abcdef0ULL },
    { 9, 600, 262, 0x123456789abcdef1ULL },
    { 23, 500, 312, 0x123456789abcdef2ULL },
    { 24, 500, 312, 0x123456789abcdef3ULL },
};

extern void minic_const_record_guard_failed(void)
    __attribute__((error("immutable record field is known")));

int main(void)
{
    if (modes[0].vtotal != 262 || modes[1].vtotal != 262 ||
        modes[2].vtotal != 312 || modes[3].vtotal != 312 ||
        modes[0].clock != 600 || modes[3].id != 24 ||
        modes[2].flags != 0x123456789abcdef2ULL)
        minic_const_record_guard_failed();
    return 0;
}
