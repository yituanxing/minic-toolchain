/* Negative: two distinct nominal record tags with identical layouts do not
 * become interchangeable merely because their pointer ABIs are both 64-bit. */
typedef struct First { unsigned long value; } First;
typedef struct Second { unsigned long value; } Second;

First *invalid_record_identity(Second *input) {
    First *result = input;
    return result;
}
