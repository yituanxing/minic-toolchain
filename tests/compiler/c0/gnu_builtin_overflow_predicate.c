/* The third operand has *type-only* semantics in GNU overflow predicates.
 * Its side effects must not be executed, even in the nonconstant path. */
_Static_assert(!__builtin_add_overflow_p(1, 2, (int)0), "small constant add");
_Static_assert(__builtin_add_overflow_p(2147483647, 1, (int)0), "signed add overflow");
_Static_assert(__builtin_add_overflow_p(255U, 1U, (unsigned char)0),
               "narrow unsigned overflow");
_Static_assert(!__builtin_sub_overflow_p(1, 2, (int)0), "small negative result fits");
_Static_assert(__builtin_sub_overflow_p(0U, 1U, (unsigned int)0),
               "unsigned subtract underflow");
_Static_assert(__builtin_mul_overflow_p(0x8000000000000000ULL, 4ULL,
                                        (unsigned long long)0), "wide multiply overflow");
_Static_assert(!__builtin_mul_overflow_p(0x100000001ULL, 1ULL,
                                         (unsigned long long)0), "wide multiply fits");
int minic_overflow_predicate_side_effect;

int minic_add_predicate_i32(int left, int right) {
    return __builtin_add_overflow_p(left, right, (int)0);
}

int minic_add_predicate_i8(int left, int right) {
    return __builtin_add_overflow_p(left, right, (signed char)0);
}

int minic_add_predicate_u32(unsigned int left, unsigned int right) {
    return __builtin_add_overflow_p(left, right,
                                    (minic_overflow_predicate_side_effect++, (unsigned int)0));
}

int minic_sub_predicate_i64(long long left, long long right) {
    return __builtin_sub_overflow_p(left, right, (long long)0);
}

int minic_mul_predicate_u32(unsigned int left, unsigned int right) {
    return __builtin_mul_overflow_p(left, right, (unsigned int)0);
}
