int invalid_statement_attribute_target(int value) {
    switch (value) {
    case 0:
        __attribute__((__always_inline__));
    default:
        return value;
    }
}
