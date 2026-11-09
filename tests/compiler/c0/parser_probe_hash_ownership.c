/* Exercise the parser's speculative static-aggregate designator scanner
 * after adding global enum constants beyond the cached hash load limit.
 * A value-copied parser probe must not free the owning parser's index. */
enum FirstNames {
    FIRST_0, FIRST_1, FIRST_2, FIRST_3,
    FIRST_4, FIRST_5, FIRST_6, FIRST_7
};

int use_first_name(void) { return FIRST_0; }

enum LaterNames {
    LATER_0, LATER_1, LATER_2, LATER_3,
    LATER_4, LATER_5, LATER_6, LATER_7,
    LATER_8
};

struct Item { int value; int extra; };
struct Item probe_array[] = {
    [LATER_0] = {1, 2},
    [LATER_8] = {3, 4}
};

int use_probe_array(void) { return probe_array[LATER_8].extra; }
