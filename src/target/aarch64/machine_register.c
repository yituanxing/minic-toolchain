#include "target/machine_register.h"

#include <string.h>

static bool aarch_machine_match(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

bool minic_aarch64_machine_fixed_register_read(const char *name, size_t length) {
    return aarch_machine_match(name, length, "sp");
}

bool minic_aarch64_machine_local_fixed_register(const char *name, size_t length) {
    unsigned int number;
    /* Current SP and x30 have explicitly admitted native paths. */
    if (aarch_machine_match(name, length, "sp") ||
        aarch_machine_match(name, length, "x30")) return true;
    /* Admit x/r0..17 only; x18 and callee-saved state remain excluded. */
    if (length < 2U || length > 3U || (name[0] != 'x' && name[0] != 'r') ||
        name[1] < '0' || name[1] > '9') return false;
    number = (unsigned int)(name[1] - '0');
    if (length == 3U) {
        if (name[2] < '0' || name[2] > '9') return false;
        number = number * 10U + (unsigned int)(name[2] - '0');
    }
    return number <= 17U;
}
