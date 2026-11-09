/* A Core conditional branch may consume a 64-bit integer on RV32 ILP32.
 * The truth value must combine both stored words.  In particular, a value
 * with only bit 32 set is true, and a masked-off high word is false.
 */
typedef unsigned int U32;
typedef unsigned long long U64;

int rv32_wide_branch_value(U32 low, U32 high) {
    U64 value = (((U64)high) << 32) | (U64)low;
    if (value) return 1;
    return 0;
}

int rv32_wide_branch_mask(U32 low, U32 high,
                          U32 mask_low, U32 mask_high) {
    U64 value = (((U64)high) << 32) | (U64)low;
    U64 mask = (((U64)mask_high) << 32) | (U64)mask_low;
    if (value & mask) return 1;
    return 0;
}

int rv32_wide_branch_ternary(U32 low, U32 high) {
    U64 value = (((U64)high) << 32) | (U64)low;
    return value ? 17 : 23;
}
