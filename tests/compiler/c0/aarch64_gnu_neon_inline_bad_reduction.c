typedef __Int8x8_t i8x8;
typedef __Int16x8_t i16x8;
i16x8 unsupported_widening_add(i8x8 source) {
    return __builtin_aarch64_saddlv8qi(source);
}
