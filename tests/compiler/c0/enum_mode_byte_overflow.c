enum too_wide_for_byte {
    BYTE_ZERO = 0,
    BYTE_TOO_WIDE = 256,
} __attribute__((__mode__(byte)));

int main(void) {
    return 0;
}
