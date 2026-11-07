/* GNU caller and GNU callee are the binary-reference ABI oracle. */
extern unsigned long minic_linux_zeropad(const void *);
extern unsigned long minic_linux_zeropad_twice(const void *);

int main(void) {
    const unsigned long words[2] = {
        0x1020304050607080UL, 0xfedcba9876543210UL
    };
    const unsigned long first = minic_linux_zeropad(words);
    const unsigned long combined = minic_linux_zeropad_twice(words);
    if (first != words[0]) return 1;
    if (combined != (words[0] ^ words[1])) return 2;
    return 42;
}
