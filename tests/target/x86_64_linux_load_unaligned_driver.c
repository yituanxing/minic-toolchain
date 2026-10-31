extern unsigned long linux_load_unaligned_zeropad(const void *);

int main(void) {
    const unsigned char data[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x00
    };
    if (linux_load_unaligned_zeropad(&data[1]) !=
        0x8877665544332211UL)
        return 1;
    return 0;
}
