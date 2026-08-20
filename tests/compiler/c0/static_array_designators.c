enum Slot { SLOT_ONE = 1, SLOT_THREE = 3 };

static const unsigned long indexed[4] = {
    [SLOT_ONE] = 7UL,
    [SLOT_THREE] = 9UL,
};

static unsigned long ranged[4] = {
    [0 ... 3] = ~0UL,
    [1] = 5UL,
};

static void fallback(void) {}

static void real0(void) {}

static void real2(void) {}

void *const syscall_shape[4] = {
    [0 ... 3] = fallback,
    [0] = real0,
    [2] = real2,
};

static const char *const names[] = {
    [0] = "zero",
    [2] = "two",
};

static int mutable_inferred[] = {
    [3] = 8,
    [1] = 4,
};

static const unsigned long chained[3][2] = {
    [1][0] = 11UL,
    [2][1] = 13UL,
};

static const unsigned long chained_sizeof_bound[64 + 1]
                                                [(((64) + ((sizeof(long) * 8)) - 1) /
                                                  ((sizeof(long) * 8)))] = {
    [0 + 1][0] = (1UL << 0),
    [0 + 1 + 1][0] = (1UL << 1),
};

extern const unsigned long chained_redeclared[64 + 1]
                                                   [(((64) + ((sizeof(long) * 8)) - 1) /
                                                     ((sizeof(long) * 8)))];
const unsigned long chained_redeclared[64 + 1]
                                      [(((64) + ((sizeof(long) * 8)) - 1) /
                                        ((sizeof(long) * 8)))] = {
    [0 + 1][0] = (1UL << 0),
    [0 + 1 + 1][0] = (1UL << 1),
};

int main(void) {
    return (int)(indexed[1] + ranged[1] + (syscall_shape[0] != 0) + (names[2] != 0) +
                 mutable_inferred[3] + chained[1][0] + chained[2][1] +
                 chained_sizeof_bound[1][0] + chained_redeclared[1][0]);
}
