/* The initial source reference requests speculative Core lowering of a static
 * helper.  After the always-false call site is removed, the helper is dead.
 * Its unsupported GNU-ASM operand should remain visible as a lowering trace,
 * but must not be counted as a live Core function failure. */
static int unused_after_core_fold(int value) {
    int result = value;
    if (result) {
        __asm__ __volatile__("nop" : "=Z"(result));
    }
    return result;
}

int main(void) {
    if (sizeof(unsigned char) == 0U) {
        return unused_after_core_fold(7);
    }
    return 0;
}
