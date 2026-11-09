typedef __Int8x8_t v8i8;
typedef __Uint8x8_t v8u8;

v8i8 add_d(v8i8 a, v8i8 b) {
    return a + b;
}

v8u8 xor_d(v8u8 a, v8u8 b) {
    return a ^ b;
}

v8i8 call_add_d(v8i8 value) {
    return add_d(value, value);
}
