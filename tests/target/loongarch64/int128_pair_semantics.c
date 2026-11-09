/* Production .i -> .s LoongArch64 typed-MIR parity for the old TI family.
 * Exercise wide value ingress/egress, unaligned memory, carry and borrow,
 * signed and unsigned comparisons, zero, unary and both shift directions.
 * The shift boundary is a runtime input: both limb paths must be present. */
typedef unsigned __int128 LoongU128;
typedef signed __int128 LoongI128;

LoongU128 loong_u128_pair_arithmetic(LoongU128 left, LoongU128 right,
    unsigned int amount) {
    volatile LoongU128 original = left;
    LoongU128 value = (original + right) - left;
    value = (value & right) ^ (left | right);
    value = ~value;
    value = -value;
    value = (value << amount) | (value >> amount);
    return value;
}

int loong_u128_pair_compare(LoongU128 left, LoongU128 right) {
    return (left < right) + 2 * (left == right) + 4 * (left == 0);
}

LoongI128 loong_i128_arithmetic_shift(LoongI128 value,
    unsigned int amount) {
    return value >> amount;
}

int loong_i128_signed_less(LoongI128 left, LoongI128 right) {
    return left < right;
}
