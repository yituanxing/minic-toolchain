/* A source-ASM positional reference with no matching source operand is
 * ill-formed. Admission must fail before any partial GNU .s is accepted. */
int main(void) {
    asm volatile("nop /* %0 */" ::: "memory");
    return 0;
}
