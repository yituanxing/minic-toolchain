#include <stdarg.h>

int minic_verify_va_list(char *arguments);

static int check_arguments(int fixed, const char *tag, ...) {
    va_list arguments;
    int result;

    va_start(arguments, tag);
    result = minic_verify_va_list(arguments);
    va_end(arguments);
    return fixed == 11 ? result : 40;
}

int main(void) {
    float value = 3.5f;

    return check_arguments(11, "x", 22, value);
}
