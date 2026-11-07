/* GNU Linux 6.6.143 frequently takes the address of a flexible array
 * member through a const-qualified record pointer.  The address expression
 * has a pointer-to-qualified-incomplete-array type; the declaration's
 * interned array descriptor remains unqualified.  Both descriptions must
 * agree after applying the enclosing lvalue qualifiers. */
struct Flexible {
    unsigned long prefix;
    unsigned char bytes[];
};
struct GnuZero {
    unsigned long prefix;
    unsigned char bytes[0];
};
struct Fixed {
    unsigned long prefix;
    unsigned char bytes[4];
};
struct Multi {
    unsigned long prefix;
    unsigned int bytes[2][3];
};

static const struct Flexible flexible = { 17UL };
static volatile struct GnuZero zero = { 19UL };
static const volatile struct Fixed fixed = { 23UL, { 1U, 2U, 3U, 4U } };
static const struct Multi multi = { 29UL, { { 1U, 2U, 3U }, { 4U, 5U, 6U } } };

const void *flexible_address(const struct Flexible *owner) {
    return (const void *)&owner->bytes;
}
volatile void *zero_address(volatile struct GnuZero *owner) {
    return (volatile void *)&owner->bytes;
}
const volatile void *fixed_address(const volatile struct Fixed *owner) {
    return (const volatile void *)&owner->bytes;
}
const void *multi_address(const struct Multi *owner) {
    return (const void *)&owner->bytes;
}

int main(void) {
    return flexible_address(&flexible) !=
               (const void *)((const unsigned char *)&flexible +
                              __builtin_offsetof(struct Flexible, bytes)) ||
           zero_address(&zero) !=
               (volatile void *)((volatile unsigned char *)&zero +
                                 __builtin_offsetof(struct GnuZero, bytes)) ||
           fixed_address(&fixed) !=
               (const volatile void *)((const volatile unsigned char *)&fixed +
                                       __builtin_offsetof(struct Fixed, bytes)) ||
           multi_address(&multi) !=
               (const void *)((const unsigned char *)&multi +
                              __builtin_offsetof(struct Multi, bytes));
}
