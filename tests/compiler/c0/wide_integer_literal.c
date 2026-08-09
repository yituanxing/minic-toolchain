long long largest_signed(void) {
    return 9223372036854775807LL;
}

long long near_largest_signed(void) {
    return 9223372036854775806LL;
}

int main(void) {
    return largest_signed() > near_largest_signed() ? 0 : 1;
}
