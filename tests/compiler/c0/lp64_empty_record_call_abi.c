/* GNU C zero-byte composites: AAPCS64 and LoongArch LP64D ignore an empty
 * named argument without shifting subsequent GP arguments. Both ABIs also
 * ignore empty members when classifying nonempty INTEGER-class records. */
struct Empty { };
struct Nested {
    unsigned long first;
    struct Empty ignored;
    unsigned long last;
};

unsigned long empty_middle(unsigned long first, struct Empty ignored,
                           unsigned long last) {
    (void)ignored;
    return first * 10UL + last;
}

unsigned long empty_on_register_boundary(unsigned long first,
                                         unsigned long second,
                                         unsigned long third,
                                         unsigned long fourth,
                                         unsigned long fifth,
                                         unsigned long sixth,
                                         unsigned long seventh,
                                         unsigned long eighth,
                                         struct Empty ignored,
                                         unsigned long ninth) {
    (void)ignored;
    return first + second + third + fourth + fifth + sixth +
           seventh + eighth + ninth;
}

unsigned long call_empty_middle(void) {
    struct Empty empty = { };
    return empty_middle(3UL, empty, 7UL) +
           empty_on_register_boundary(1UL, 2UL, 3UL, 4UL, 5UL,
                                      6UL, 7UL, 8UL, empty, 9UL);
}

struct Empty return_empty(void) {
    struct Empty empty = { };
    return empty;
}

void call_return_empty(void) {
    struct Empty empty = return_empty();
    (void)empty;
}

struct Nested return_nonempty_with_empty_member(unsigned long value) {
    struct Nested nested = { .first = value, .last = value + 1UL };
    return nested;
}

unsigned long call_nonempty_with_empty_member(void) {
    struct Nested nested = return_nonempty_with_empty_member(11UL);
    return nested.first + nested.last;
}
