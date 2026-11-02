/* Source enums can have byte storage but must not inherit the backend's
 * ordinary 8-bit integer checked-overflow mode merely because CoreType
 * has correctly lowered their execution representation to an integer. */
enum signed_byte_kind {
    SIGNED_BYTE_ZERO = 0,
    SIGNED_BYTE_MAX = 127
} __attribute__((__mode__(byte)));

#ifdef MINIC_OVERFLOW_ENUM_NEGATIVE
int checked_enum_add(int left, int right, enum signed_byte_kind *result) {
    return __builtin_add_overflow(left, right, result);
}

int checked_enum_predicate(int left, int right) {
    return __builtin_add_overflow_p(left, right, (enum signed_byte_kind)0);
}
#endif

int checked_char_add(int left, int right, signed char *result) {
    return __builtin_add_overflow(left, right, result);
}
