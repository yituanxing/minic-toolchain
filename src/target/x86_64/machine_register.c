#include "target/machine_register.h"

#include <string.h>

static bool x86_machine_match(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

bool minic_x86_64_machine_fixed_register_read(const char *name, size_t length) {
    return x86_machine_match(name, length, "rsp");
}

bool minic_x86_64_machine_register_clobber(const char *name, size_t length) {
    static const char *const caller_scratch[] = {
        "cc", "dx", "rdx", "edx", "rax", "eax", "rcx", "ecx",
        "rsi", "esi", "rdi", "edi", "r8", "r9", "r10", "r11"
    };
    size_t i;
    for (i = 0U; i < sizeof(caller_scratch) / sizeof(caller_scratch[0]); ++i)
        if (x86_machine_match(name, length, caller_scratch[i])) return true;
    return false;
}

bool minic_x86_64_machine_local_fixed_register(const char *name, size_t length) {
    /* GNU accepts '%' on locals. Source approval does not reserve a GPR. */
    if (length > 1U && name[0] == '%') { ++name; --length; }
    return !x86_machine_match(name, length, "cc") &&
           minic_x86_64_machine_register_clobber(name, length);
}
