typedef __Uint8x16_t vector128_u8;

void xor_vector128(vector128_u8 *dst, const vector128_u8 *left,
                   const vector128_u8 *right) {
    *dst = *left ^ *right;
}
