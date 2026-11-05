/* This old unsupported Core shape must remain a hard diagnostic. Neither
 * the legacy emitter nor typed Loong MIR supports 128x128 multiplication. */
unsigned __int128 loong_u128_unsupported_multiply(
    unsigned __int128 left, unsigned __int128 right) {
    return left * right;
}
