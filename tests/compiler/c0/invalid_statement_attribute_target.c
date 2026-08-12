int invalid_statement_attribute_target(int value)
{
    switch (value) {
    case 0:
        __attribute__((__unused__));
    default:
        return value;
    }
}
