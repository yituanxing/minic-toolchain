#include <stdint.h>

int minic_verify_va_list(void *arguments) {
    const uint64_t *words = (const uint64_t *)arguments;
    union {
        double value;
        uint64_t bits;
    } expected;

    expected.value = 3.5;
    if ((int64_t)words[0] != 22) {
        return 41;
    }
    if (words[1] != expected.bits) {
        return 42;
    }
    return 0;
}
