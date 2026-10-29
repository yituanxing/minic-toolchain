#include <stdarg.h>

extern int mini_va_restart(unsigned int tag, ...);

int mini_va_take_one(va_list ap) {
    return va_arg(ap, int);
}

int main(void) {
    if (mini_va_restart(1U, 21) != 42) return 1;
    if (mini_va_restart(0U, 27) != 27) return 2;
    return 0;
}
