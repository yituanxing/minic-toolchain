int main(void) {
    __asm__ volatile("nop" : : "i"(31));
    return 0;
}
