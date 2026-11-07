/* Deliberately unsupported source-ASM operand. The failed IF-body lowering
 * must release every allocated per-local Core facts snapshot before returning
 * its existing diagnostic; this is a negative ownership regression. */
int unsupported_if_body(int condition) {
    int first = 3;
    int second = 7;
    int third = 11;
    if (condition) {
        __asm__ __volatile__("nop" : "=Z"(first));
    } else {
        second += third;
    }
    return first + second;
}

int unsupported_else_body(int condition) {
    int value = 4;
    int companion = 6;
    if (condition) {
        value += companion;
    } else {
        __asm__ __volatile__("nop" : "=Z"(value));
    }
    return value + companion;
}
