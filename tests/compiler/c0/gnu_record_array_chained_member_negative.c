/* Only declared members may follow a GNU [i].member designator. */
struct MinicNegativeRecord { unsigned valid; };
static const struct MinicNegativeRecord invalid[2] = {
    [0].missing = 1U,
};
int minic_record_chained_bad(void) { return (int)invalid[0].valid; }
