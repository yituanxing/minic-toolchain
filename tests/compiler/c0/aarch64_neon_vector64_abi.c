/* AAPCS64 D-register ABI: named signed/unsigned byte vectors have exact
 * eight-byte lane storage, survive direct/indirect calls and stack overflow,
 * and return through v0 rather than an integer-class GPR. */
typedef __Int8x8_t v8i8;
typedef __Uint8x8_t v8u8;

union lanes8 {
    v8i8 signed_vector;
    v8u8 unsigned_vector;
    unsigned char byte[8];
};

static union lanes8 left, right, result;

__attribute__((noinline)) v8i8 add8(v8i8 a, v8i8 b) {
    return a + b;
}

__attribute__((noinline)) v8u8 xor8(v8u8 a, v8u8 b) {
    return a ^ b;
}

__attribute__((noinline)) v8u8 ninth8(
    v8u8 a, v8u8 b, v8u8 c, v8u8 d,
    v8u8 e, v8u8 f, v8u8 g, v8u8 h, v8u8 i) {
    return a ^ i;
}

int main(void) {
    unsigned int i;
    v8u8 (*indirect)(v8u8, v8u8) = xor8;
    for (i = 0U; i < 8U; ++i) {
        left.byte[i] = (unsigned char)(i * 17U + 1U);
        right.byte[i] = (unsigned char)(i * 3U + 11U);
    }
    result.signed_vector = add8(left.signed_vector, right.signed_vector);
    for (i = 0U; i < 8U; ++i)
        if (result.byte[i] != (unsigned char)(left.byte[i] + right.byte[i]))
            return 1;
    result.unsigned_vector = indirect(left.unsigned_vector, right.unsigned_vector);
    for (i = 0U; i < 8U; ++i)
        if (result.byte[i] != (unsigned char)(left.byte[i] ^ right.byte[i]))
            return 2;
    result.unsigned_vector = ninth8(
        left.unsigned_vector, left.unsigned_vector, left.unsigned_vector,
        left.unsigned_vector, left.unsigned_vector, left.unsigned_vector,
        left.unsigned_vector, left.unsigned_vector, right.unsigned_vector);
    for (i = 0U; i < 8U; ++i)
        if (result.byte[i] != (unsigned char)(left.byte[i] ^ right.byte[i]))
            return 3;
    return 0;
}
