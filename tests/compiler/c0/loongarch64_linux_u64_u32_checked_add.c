/* Original Linux fs/udf/inode.c udf_current_aext() type shape:
 * check_add_overflow(sizeof(struct allocExtDesc), le32_to_cpu(...), &alen)
 * has unsigned long 64 + unsigned int 32 -> signed int 32. */
int udf_checked_add_u64_u32(unsigned long length,
                            unsigned int delta, int *written) {
    return __builtin_add_overflow(length, delta, written);
}
int udf_checked_add_u32_u64(unsigned int delta,
                            unsigned long length, int *written) {
    return __builtin_add_overflow(delta, length, written);
}
