static long long signed_value = 3LL;
static unsigned long long unsigned_value = 4ULL;

long long add_long_long(long long left, unsigned int right) {
    return left + right;
}

unsigned long long add_unsigned_long_long(unsigned long long left, unsigned long right) {
    return left + right;
}

int main(void) {
    return sizeof(long long) == 8 && sizeof(unsigned long long) == 8 &&
                   add_long_long(signed_value, 2U) == 5LL &&
                   add_unsigned_long_long(unsigned_value, 2UL) == 6ULL
               ? 0
               : 1;
}
