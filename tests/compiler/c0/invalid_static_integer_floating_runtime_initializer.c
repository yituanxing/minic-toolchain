double get_divisor(void);
struct FreqTable { unsigned int hid_div; };
static const struct FreqTable invalid = { .hid_div = 2 * get_divisor() - 1 };
