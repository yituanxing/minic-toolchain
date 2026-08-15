typedef _Bool bool;

struct full_unit_pad {
    char tag;
    int :32;
    char tail;
};

struct zero_width_barrier {
    char tag;
    int :0;
    char tail;
};

struct bool_bits {
    bool first : 1;
    bool second : 1;
    char tail;
};

struct signed_bits {
    int signed_three : 3;
    char tail;
};

struct int_bits {
    unsigned int low : 10;
    unsigned int high : 12;
    char tail;
};

struct short_boundary_bits {
    unsigned short low : 10;
    unsigned short high : 12;
    char tail;
};

struct named_zero_barrier {
    unsigned int first : 1;
    unsigned int :0;
    unsigned int second : 1;
    char tail;
};

static struct bool_bits static_bool_bits = {
    .second = 1,
    .first = 1,
    .tail = 5,
};

static struct int_bits static_int_bits = {
    .high = 0xabc,
    .low = 0x155,
    .tail = 7,
};

unsigned long full_unit_tail_offset(void) {
    return __builtin_offsetof(struct full_unit_pad, tail);
}

unsigned long zero_width_tail_offset(void) {
    return __builtin_offsetof(struct zero_width_barrier, tail);
}

unsigned long bool_tail_offset(void) {
    return __builtin_offsetof(struct bool_bits, tail);
}

unsigned long signed_tail_offset(void) {
    return __builtin_offsetof(struct signed_bits, tail);
}

unsigned long signed_record_size(void) {
    return sizeof(struct signed_bits);
}

unsigned long int_tail_offset(void) {
    return __builtin_offsetof(struct int_bits, tail);
}

unsigned long short_boundary_tail_offset(void) {
    return __builtin_offsetof(struct short_boundary_bits, tail);
}

unsigned long named_zero_tail_offset(void) {
    return __builtin_offsetof(struct named_zero_barrier, tail);
}

int read_bool_second(struct bool_bits *bits) {
    return bits->second;
}

void write_bool_second(struct bool_bits *bits, int value) {
    bits->second = value;
}

int read_signed_three(struct signed_bits *bits) {
    return bits->signed_three;
}

unsigned int read_int_high(struct int_bits *bits) {
    return bits->high;
}

void add_int_high(struct int_bits *bits, unsigned int value) {
    bits->high += value;
}

unsigned int increment_barrier_second(struct named_zero_barrier *bits) {
    return ++bits->second;
}

int main(void) {
    return full_unit_tail_offset() == 8UL && zero_width_tail_offset() == 4UL &&
                   bool_tail_offset() == 1UL && signed_tail_offset() == 1UL &&
                   signed_record_size() == 4UL && int_tail_offset() == 3UL &&
                   short_boundary_tail_offset() == 4UL && named_zero_tail_offset() == 5UL
               ? 0
               : 1;
}
