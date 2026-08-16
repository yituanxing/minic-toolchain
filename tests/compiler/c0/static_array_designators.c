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

int main(void) {
    return (int)(indexed[1] + ranged[1] + (syscall_shape[0] != 0) + (names[2] != 0) +
                 mutable_inferred[3]);
}
