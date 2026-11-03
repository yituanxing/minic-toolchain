typedef signed __int128 signed128_t;
typedef unsigned __int128 unsigned128_t;

signed128_t loong_core_negate_i128(signed128_t value) {
    return -value;
}

unsigned128_t loong_core_not_u128(unsigned128_t value) {
    return ~value;
}

unsigned128_t loong_core_add_u128(unsigned128_t left,
                                  unsigned128_t right) {
    return left + right;
}

int loong_core_less_i128(signed128_t left, signed128_t right) {
    return left < right;
}

int loong_core_less_u128(unsigned128_t left, unsigned128_t right) {
    return left < right;
}

unsigned128_t loong_core_shift_left_u128(unsigned128_t left,
                                         unsigned int right) {
    return left << right;
}

signed128_t loong_core_shift_right_i128(signed128_t left,
                                       unsigned int right) {
    return left >> right;
}

unsigned128_t loong_core_shift_right_u128(unsigned128_t left,
                                          unsigned int right) {
    return left >> right;
}

int loong_core_equal_pointer(const void *left, const void *right) {
    return left == right;
}

_Bool loong_core_checked_add_i32_u32(int left, unsigned int right,
                                     int *result) {
    return __builtin_add_overflow(left, right, result);
}

_Bool loong_core_checked_add_u32_i32(unsigned int left, int right,
                                     int *result) {
    return __builtin_add_overflow(left, right, result);
}
