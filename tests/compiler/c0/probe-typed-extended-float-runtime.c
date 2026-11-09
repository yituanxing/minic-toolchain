/* A real C symbol named like a compiler-runtime helper must remain an
 * ordinary external call.  Core's intrinsic wide arithmetic is separately
 * identified by its typed operation, regardless of symbol spelling. */
long double __addtf3(long double left, long double right) {
    return left + right + 2.0L;
}

long double call_named_helper(long double left, long double right) {
    return __addtf3(left, right);
}

long double call_typed_add(long double left, long double right) {
    return left + right;
}

int call_typed_less(long double left, long double right) {
    return left < right;
}

int call_typed_less_equal(long double left, long double right) {
    return left <= right;
}

int main(void) {
    long double left = 1.0L;
    long double right = 2.0L;
    return call_named_helper(left, right) == 5.0L &&
           call_typed_add(left, right) == 3.0L &&
           call_typed_less(left, right) == 1 &&
           call_typed_less(right, left) == 0 &&
           call_typed_less_equal(left, left) == 1 &&
           call_typed_less_equal(right, left) == 0 ? 42 : 1;
}
