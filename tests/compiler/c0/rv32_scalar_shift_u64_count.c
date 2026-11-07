/* RV32 Linux BPF authentic owners use i64 shift counts for i32 scalars.
 * A scalar result must not be mistaken for a two-register i64 operand. */
unsigned int rv32_shift_left_u64_count(unsigned int lhs,
    unsigned long long count) {
    return lhs << count;
}

unsigned int rv32_shift_right_u64_count(unsigned int lhs,
    unsigned long long count) {
    return lhs >> count;
}

int rv32_shift_right_i64_count(int lhs, long long count) {
    return lhs >> count;
}

unsigned short rv32_shift_right_narrow_u64_count(unsigned short lhs,
    unsigned long long count) {
    return (unsigned short)(lhs >> count);
}
