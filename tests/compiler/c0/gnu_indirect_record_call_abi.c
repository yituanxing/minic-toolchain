typedef struct {
    unsigned value;
} SmallRegister;

typedef struct {
    unsigned long low, high;
} PairRegister;

typedef struct {
    unsigned long a, b, c, d, e;
} WideRegister;

typedef SmallRegister (*SmallReader)(unsigned);
typedef PairRegister (*PairReader)(unsigned);
typedef WideRegister (*WideReader)(unsigned);

static SmallRegister small_read(unsigned value) {
    SmallRegister result = { .value = 3U + value };
    return result;
}

static PairRegister pair_read(unsigned value) {
    PairRegister result = { .low = value + 7U, .high = value + 11U };
    return result;
}

static WideRegister wide_read(unsigned value) {
    WideRegister result = {
        .a = value, .b = value + 1U, .c = value + 2U,
        .d = value + 3U, .e = value + 4U
    };
    return result;
}

static unsigned consume_small(SmallRegister reg, unsigned amount) {
    return reg.value + amount;
}

unsigned indirect_small_record(unsigned value, SmallReader reader) {
    return consume_small(reader(value), 7U);
}

unsigned long indirect_pair_record(unsigned value, PairReader reader) {
    PairRegister reg = reader(value);
    return reg.low * 2UL + reg.high;
}

unsigned long indirect_wide_record(unsigned value, WideReader reader) {
    WideRegister reg = reader(value);
    return reg.a + reg.b + reg.c + reg.d + reg.e;
}

int main(void) {
    return indirect_small_record(17U, small_read) == 27U &&
           indirect_pair_record(10U, pair_read) == 55UL &&
           indirect_wide_record(10U, wide_read) == 60UL
               ? 0 : 1;
}
