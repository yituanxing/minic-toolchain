static unsigned long compiler_barrier(unsigned long dividend, unsigned int divisor) {
    asm("" : "+rm"(dividend));
    return dividend - divisor;
}

int main(void) {
    return compiler_barrier(9UL, 4U) == 5UL ? 0 : 1;
}
