#include "target/machine_register.h"

#include <string.h>

bool minic_loongarch64_machine_fixed_register_read(const char *name, size_t length) {
    /* TP/SP/r21 are the currently qualified Linux register aliases. */
    return (length == 3U &&
            (memcmp(name, "$tp", 3U) == 0 || memcmp(name, "$sp", 3U) == 0)) ||
           (length == 4U && memcmp(name, "$r21", 4U) == 0);
}

bool minic_loongarch64_machine_fixed_register_write(const char *name, size_t length) {
    /* Reserved TP/r21 are writable; SP must never be written here. */
    return (length == 3U && memcmp(name, "$tp", 3U) == 0) ||
           (length == 4U && memcmp(name, "$r21", 4U) == 0);
}

bool minic_loongarch64_machine_local_fixed_register(const char *name, size_t length) {
    return length == 2U && ((name[0] == 't' && name[1] == '0') ||
                            (name[0] == 'a' &&
                             (name[1] == '0' || name[1] == '1' || name[1] == '7')));
}
