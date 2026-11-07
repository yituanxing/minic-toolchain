/* Negative: an aligned pointer-to-const volatile record must never become a
 * mutable pointer just because both record views have identical byte layout. */
typedef struct Guid { unsigned char bytes[16]; } Guid;
typedef Guid AlignedGuid __attribute__((aligned(4)));

AlignedGuid *invalid_drop_pointee_cv(const volatile AlignedGuid *input) {
    AlignedGuid *result = input;
    return result;
}
